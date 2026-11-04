#include "motif/mscz/parser.h"

#include <fmt/format.h>
#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <vector>

#include "motif/mscz/archive.h"
#include "motif/score/ticks.h"

namespace motif::mscz {
namespace {

using score::Element;
using score::ElementKind;
using score::Fraction;
using score::Note;
using score::Pitch;
using score::Score;

void set_error(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

/// Text of a direct child element, or "" when absent.
std::string child_text(const pugi::xml_node& node, const char* name) {
  return node.child(name).text().as_string();
}

/// Parses "3/8" or "-3/8" into a Fraction. Returns 0/1 on anything unrecognised.
Fraction parse_fraction(std::string_view text) {
  const auto slash = text.find('/');
  if (slash == std::string_view::npos) return Fraction(0, 1);

  int num = 0;
  int den = 1;
  const auto num_sv = text.substr(0, slash);
  const auto den_sv = text.substr(slash + 1);
  if (std::from_chars(num_sv.data(), num_sv.data() + num_sv.size(), num).ec != std::errc{}) {
    return Fraction(0, 1);
  }
  if (std::from_chars(den_sv.data(), den_sv.data() + den_sv.size(), den).ec != std::errc{}) {
    return Fraction(0, 1);
  }
  return Fraction(num, den);
}

/// Tracks the tuplet ratio in force at the cursor.
///
/// MuseScore writes `<Tuplet>` as a sibling *before* the notes it governs and closes it
/// with `<endTuplet/>`, so this is a stack rather than a property of each chord.
class TupletStack {
 public:
  /// Opens a tuplet, recording it in `voice` so engraving can bracket it.
  void push(int normal_notes, int actual_notes, score::Voice& voice) {
    voice.tuplets.push_back({actual_notes, normal_notes, innermost()});
    ratios_.push_back({normal_notes, actual_notes, static_cast<int>(voice.tuplets.size()) - 1});
  }
  void pop() {
    if (!ratios_.empty()) ratios_.pop_back();
  }
  [[nodiscard]] bool active() const { return !ratios_.empty(); }

  /// The open tuplet nearest the notes, as an index into Voice::tuplets, or -1.
  [[nodiscard]] int innermost() const { return ratios_.empty() ? -1 : ratios_.back().index; }

  [[nodiscard]] int scale(int ticks) const {
    for (const auto& ratio : ratios_) {
      ticks = score::apply_tuplet(ticks, ratio.normal, ratio.actual);
    }
    return ticks;
  }

 private:
  struct Ratio {
    int normal;
    int actual;
    int index;
  };
  std::vector<Ratio> ratios_;
};

/// True for a grace note's chord, which MuseScore marks with a child such as
/// <acciaccatura/> or <grace16/>. Grace notes take no time in the bar.
bool is_grace(const pugi::xml_node& chord) {
  for (const auto child : chord.children()) {
    const std::string_view name = child.name();
    if (name == "acciaccatura" || name == "appoggiatura" || name.starts_with("grace")) {
      return true;
    }
  }
  return false;
}

/// Reads a `<Note>`, including whether it is tied to its neighbours.
Note parse_note(const pugi::xml_node& node) {
  Note note;
  note.eid = child_text(node, "eid");
  note.pitch.midi = node.child("pitch").text().as_int(60);
  note.pitch.tpc = node.child("tpc").text().as_int(14);
  note.velocity = node.child("velocity").text().as_int(-1);

  // A tie is written as a Spanner on the note it touches: a `<next>` anchor means the
  // tie continues forward from here, a `<prev>` anchor means it arrives here.
  for (const auto spanner : node.children("Spanner")) {
    if (std::string_view(spanner.attribute("type").as_string()) != "Tie") continue;
    if (spanner.child("next")) note.tie_out = true;
    if (spanner.child("prev")) note.tie_in = true;
  }
  return note;
}

/// Reads a `<Chord>` or `<Rest>`, resolving its duration in ticks.
///
/// `measure_ticks` supplies the length of a whole-measure rest, whose duration is
/// defined by the enclosing measure rather than by a note value.
Element parse_element(const pugi::xml_node& node, ElementKind kind, int division,
                      int measure_ticks, const TupletStack& tuplets, int cursor) {
  Element el;
  el.kind = kind;
  el.eid = child_text(node, "eid");
  el.onset = cursor;
  el.dots = node.child("dots").text().as_int(0);
  el.in_tuplet = tuplets.active();
  el.tuplet = tuplets.innermost();

  el.duration_type = child_text(node, "durationType");
  if (const auto ticks = score::duration_ticks(el.duration_type, el.dots, division)) {
    el.ticks = tuplets.scale(*ticks);
  } else if (const auto duration = node.child("duration")) {
    // Whole-measure rests carry an explicit `<duration>` such as 4/4.
    el.ticks = parse_fraction(duration.text().as_string()).to_ticks(division);
  } else {
    el.ticks = measure_ticks;
  }

  if (kind == ElementKind::Chord) {
    for (const auto note : node.children("Note")) {
      el.notes.push_back(parse_note(note));
    }
    for (const auto artic : node.children("Articulation")) {
      std::string subtype = child_text(artic, "subtype");
      if (!subtype.empty()) el.articulations.push_back(std::move(subtype));
    }
    // Sorted so that reordering in the file is not mistaken for a musical change.
    std::sort(el.articulations.begin(), el.articulations.end());
  }

  return el;
}

/// State carried from measure to measure while walking one staff.
struct StaffCursor {
  Fraction time_sig{4, 4};
  int abs_tick = 0;
  int number = 1;
};

/// Parses one `<voice>`, appending any measure-level attributes it declares.
score::Voice parse_voice(const pugi::xml_node& voice_node, int index, int division,
                         score::Measure& measure, int& measure_ticks) {
  score::Voice voice;
  voice.index = index;

  TupletStack tuplets;
  int cursor = 0;

  for (const auto node : voice_node.children()) {
    const std::string_view name = node.name();

    if (name == "Chord" && is_grace(node)) {
      // Not modelled yet. Read as an ordinary chord, it would push every later note in
      // the voice off its beat.
      continue;
    }
    if (name == "Chord" || name == "Rest") {
      const auto kind = (name == "Chord") ? ElementKind::Chord : ElementKind::Rest;
      auto el = parse_element(node, kind, division, measure_ticks, tuplets, cursor);
      cursor += el.ticks;
      voice.elements.push_back(std::move(el));

    } else if (name == "Tuplet") {
      tuplets.push(node.child("normalNotes").text().as_int(1),
                   node.child("actualNotes").text().as_int(1), voice);

    } else if (name == "endTuplet") {
      tuplets.pop();

    } else if (name == "TimeSig") {
      score::TimeSig ts;
      ts.n = node.child("sigN").text().as_int(4);
      ts.d = node.child("sigD").text().as_int(4);
      measure.time_sig = ts;
      // A time signature declared here governs this measure, so whole-measure rests
      // later in the same voice must see the updated length. An irregular measure keeps
      // the explicit length from its `len` attribute, which overrides the signature.
      if (!measure.irregular) {
        measure.length = ts.as_fraction();
        measure_ticks = measure.length.to_ticks(division);
      }

    } else if (name == "KeySig") {
      score::KeySig ks;
      ks.concert = node.child("concertKey").text().as_int(0);
      if (const auto actual = node.child("actualKey")) {
        ks.actual = actual.text().as_int(0);
      }
      measure.key_sig = ks;

    } else if (name == "Clef") {
      score::Clef clef;
      clef.concert = child_text(node, "concertClefType");
      clef.transposing = child_text(node, "transposingClefType");
      measure.clef = clef;

    } else if (name == "location") {
      // Repositions the cursor, e.g. when a voice does not begin at the barline.
      cursor += parse_fraction(node.child("fractions").text().as_string()).to_ticks(division);
    }
    // Anything else (Tempo, Dynamic, Spanner, Harmony, ...) is not modelled yet and is
    // skipped rather than treated as an error.
  }

  return voice;
}

score::Measure parse_measure(const pugi::xml_node& node, int division, StaffCursor& cursor) {
  score::Measure measure;
  measure.eid = child_text(node, "eid");
  measure.start_tick = cursor.abs_tick;
  measure.length = cursor.time_sig;

  // An explicit `len` marks an irregular measure -- a pickup, or a bar split across a
  // system -- whose length differs from the prevailing time signature.
  if (const auto len_attr = node.attribute("len")) {
    measure.length = parse_fraction(len_attr.as_string());
    measure.irregular = true;
  }

  int measure_ticks = measure.length.to_ticks(division);

  const auto voice_nodes = node.children("voice");
  if (voice_nodes.begin() == voice_nodes.end()) {
    // Older files place chords directly under the measure with no <voice> wrapper.
    measure.voices.push_back(parse_voice(node, 1, division, measure, measure_ticks));
  } else {
    int index = 1;
    for (const auto voice_node : voice_nodes) {
      measure.voices.push_back(parse_voice(voice_node, index, division, measure, measure_ticks));
      ++index;
    }
  }

  // A time signature inside the measure replaces the prevailing one from here on --
  // including when declared in a pickup, which governs the measures that follow it even
  // though the pickup itself keeps its own short length.
  if (measure.time_sig) {
    cursor.time_sig = measure.time_sig->as_fraction();
  }
  if (!measure.irregular) {
    measure.length = cursor.time_sig;
    measure_ticks = measure.length.to_ticks(division);
  }

  measure.number = node.attribute("number").as_int(cursor.number);
  cursor.number = measure.number + 1;
  cursor.abs_tick += measure_ticks;
  return measure;
}

/// Reads the parts, and each part's instrument clefs (one per staff) into `default_clefs`.
void parse_parts(const pugi::xml_node& score_node, Score& score,
                 std::vector<std::vector<std::string>>& default_clefs) {
  int next_staff_id = 1;

  for (const auto part_node : score_node.children("Part")) {
    score::Part part;
    part.id = part_node.attribute("id").as_string();
    part.track_name = child_text(part_node, "trackName");

    std::vector<std::string> clefs;
    if (const auto instrument = part_node.child("Instrument")) {
      part.long_name = child_text(instrument, "longName");
      part.short_name = child_text(instrument, "shortName");
      part.instrument_id = child_text(instrument, "instrumentId");
      part.instrument_name = child_text(instrument, "trackName");
      for (const auto clef : instrument.children("clef")) {
        const int staff = clef.attribute("staff").as_int(1);
        if (staff < 1) continue;
        if (clefs.size() < static_cast<std::size_t>(staff)) {
          clefs.resize(static_cast<std::size_t>(staff));
        }
        clefs[static_cast<std::size_t>(staff) - 1] = clef.text().as_string();
      }
    }
    default_clefs.push_back(std::move(clefs));

    // Staff elements inside a Part usually carry no id, so ownership is positional:
    // each part consumes as many score-level staves as it declares.
    for (const auto staff_node : part_node.children("Staff")) {
      const int id = staff_node.attribute("id").as_int(next_staff_id);
      part.staff_ids.push_back(id);
      next_staff_id = id + 1;
    }

    score.parts.push_back(std::move(part));
  }
}

void parse_staves(const pugi::xml_node& score_node, Score& score,
                  const std::vector<std::vector<std::string>>& default_clefs) {
  for (const auto staff_node : score_node.children("Staff")) {
    const auto id_attr = staff_node.attribute("id");
    if (!id_attr) continue;  // a Part's own <Staff> stub, not a score-level staff

    score::Staff staff;
    staff.id = id_attr.as_int();

    StaffCursor cursor;
    for (const auto measure_node : staff_node.children("Measure")) {
      staff.measures.push_back(parse_measure(measure_node, score.division, cursor));
    }

    score.staves.push_back(std::move(staff));
  }

  // Link staves back to the part that owns them.
  for (std::size_t p = 0; p < score.parts.size(); ++p) {
    int index_in_part = 0;
    for (const int staff_id : score.parts[p].staff_ids) {
      for (auto& staff : score.staves) {
        if (staff.id != staff_id) continue;
        staff.part_index = static_cast<int>(p);
        staff.index_in_part = index_in_part;
        const auto& clefs = default_clefs[p];
        const auto i = static_cast<std::size_t>(index_in_part);
        if (i < clefs.size() && !clefs[i].empty()) staff.default_clef = clefs[i];
      }
      ++index_in_part;
    }
  }
}

}  // namespace

std::optional<Score> parse_mscx(std::string_view xml, std::string* error) {
  pugi::xml_document doc;
  const auto result = doc.load_buffer(xml.data(), xml.size());
  if (!result) {
    set_error(error, fmt::format("invalid MuseScore XML: {} at offset {}", result.description(),
                                 result.offset));
    return std::nullopt;
  }

  const auto score_node = doc.child("museScore").child("Score");
  if (!score_node) {
    set_error(error, "no <Score> element found (is this a MuseScore file?)");
    return std::nullopt;
  }

  Score score;
  score.division = score_node.child("Division").text().as_int(480);
  if (score.division <= 0) score.division = 480;

  for (const auto tag : score_node.children("metaTag")) {
    score.meta[tag.attribute("name").as_string()] = tag.text().as_string();
  }

  std::vector<std::vector<std::string>> default_clefs;
  parse_parts(score_node, score, default_clefs);
  parse_staves(score_node, score, default_clefs);

  if (score.staves.empty()) {
    set_error(error, "score contains no staves");
    return std::nullopt;
  }

  return score;
}

std::optional<Score> parse_score(std::string_view bytes, std::string* error) {
  const auto xml = extract_mscx(bytes, error);
  if (!xml) return std::nullopt;
  return parse_mscx(*xml, error);
}

std::optional<Score> parse_score_file(const std::filesystem::path& path, std::string* error) {
  const auto xml = extract_mscx_from_file(path, error);
  if (!xml) return std::nullopt;
  return parse_mscx(*xml, error);
}

}  // namespace motif::mscz
