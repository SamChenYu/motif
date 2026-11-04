#include "motif/export/mei.h"

#include <fmt/format.h>
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "motif/score/pitch.h"

namespace motif::mei {
namespace {

using score::Element;
using score::Note;
using score::Voice;

/// MuseScore's duration types, and the MEI @dur of each.
constexpr std::array<std::pair<std::string_view, std::string_view>, 12> kValues{{
    {"long", "long"},
    {"breve", "breve"},
    {"whole", "1"},
    {"half", "2"},
    {"quarter", "4"},
    {"eighth", "8"},
    {"16th", "16"},
    {"32nd", "32"},
    {"64th", "64"},
    {"128th", "128"},
    {"256th", "256"},
    {"512th", "512"},
}};

/// A written note value: MEI's @dur, and how many dots.
struct Value {
  std::string dur;
  int dots = 0;
};

/// Ticks in a whole note's 1/`d`, or 0 when that isn't a whole number of ticks.
int ticks_of(int d, int division) { return (4 * division) % d == 0 ? 4 * division / d : 0; }

/// The note value that lasts exactly `ticks`, with up to three dots.
std::optional<Value> value_from_ticks(long long ticks, int division) {
  for (int d = 1; d <= 128; d *= 2) {
    const long long base = ticks_of(d, division);
    if (base == 0) break;
    long long length = base;
    long long add = base;
    for (int dots = 0; dots <= 3; ++dots) {
      if (length == ticks) return Value{std::to_string(d), dots};
      if (add % 2 != 0) break;
      add /= 2;
      length += add;
    }
  }
  return std::nullopt;
}

/// Splits a gap of `ticks` into note values, longest first, for invisible spaces. Any
/// remainder too short for a 128th is dropped.
std::vector<std::string> space_values(long long ticks, int division) {
  std::vector<std::string> out;
  for (int d = 1; d <= 128 && ticks > 0; d *= 2) {
    const int base = ticks_of(d, division);
    if (base == 0) break;
    while (ticks >= base) {
      out.push_back(std::to_string(d));
      ticks -= base;
    }
  }
  return out;
}

/// The written value of a chord or rest.
Value element_value(const Element& el, const Voice& voice, int division) {
  for (const auto& [type, dur] : kValues) {
    if (type == el.duration_type) return {std::string(dur), el.dots};
  }
  // No usable durationType (older files): undo the tuplets to recover the written value.
  long long ticks = el.ticks;
  for (int t = el.tuplet; t >= 0; t = voice.tuplets[static_cast<std::size_t>(t)].parent) {
    const auto& tuplet = voice.tuplets[static_cast<std::size_t>(t)];
    if (tuplet.normal > 0) ticks = ticks * tuplet.actual / tuplet.normal;
  }
  return value_from_ticks(ticks, division).value_or(Value{"4", 0});
}

/// The tuplets an element sits in, outermost first, as indices into Voice::tuplets.
std::vector<int> tuplet_chain(const Element& el, const Voice& voice) {
  std::vector<int> chain;
  for (int t = el.tuplet; t >= 0; t = voice.tuplets[static_cast<std::size_t>(t)].parent) {
    chain.push_back(t);
  }
  std::reverse(chain.begin(), chain.end());
  return chain;
}

/// How many of the tuplets in `open` the next element, inside `chain`, stays within.
std::size_t shared_tuplets(const std::vector<int>& open, const std::vector<int>& chain) {
  std::size_t common = 0;
  while (common < open.size() && common < chain.size() && open[common] == chain[common]) ++common;
  return common;
}

/// A gap of real `ticks`, in the written ticks of a space inside `tuplets`: inside a
/// triplet, written time runs half as fast again.
long long written_gap(long long ticks, const std::vector<int>& tuplets, const Voice& voice) {
  for (const int t : tuplets) {
    const auto& tuplet = voice.tuplets[static_cast<std::size_t>(t)];
    if (tuplet.normal > 0) ticks = ticks * tuplet.actual / tuplet.normal;
  }
  return ticks;
}

/// The quarter notes a written value lasts, inside `tuplets` (the first `depth` of them).
Quarters sounding(const Value& value, const std::vector<int>& tuplets, std::size_t depth,
                  const Voice& voice) {
  long long num = 4;
  long long den = 1;
  if (value.dur == "long") {
    num = 16;
  } else if (value.dur == "breve") {
    num = 8;
  } else {
    den = std::stoll(value.dur);
  }
  if (value.dots > 0) {
    num *= (1LL << (value.dots + 1)) - 1;
    den *= 1LL << value.dots;
  }
  for (std::size_t i = 0; i < depth && i < tuplets.size(); ++i) {
    const auto& tuplet = voice.tuplets[static_cast<std::size_t>(tuplets[i])];
    if (tuplet.actual <= 0 || tuplet.normal <= 0) continue;
    num *= tuplet.normal;
    den *= tuplet.actual;
  }
  Quarters q;
  q.add(num, den);
  return q;
}

struct ClefSpec {
  std::string shape = "G";
  int line = 2;
  int dis = 0;            ///< octave displacement: 8 or 15
  bool below = true;
};

/// MEI's clef for a MuseScore clef type: "G", "F8vb", "C3", "G15ma", "PERC", ...
ClefSpec clef_spec(std::string_view type) {
  ClefSpec c;
  if (type.empty()) return c;
  if (type.starts_with("PERC")) return {"perc", 3, 0, true};
  if (type.starts_with("TAB")) return {"TAB", 5, 0, true};

  switch (type[0]) {
    case 'F':
      c.shape = "F";
      c.line = type == "F_B" ? 3 : type == "F_C" ? 5 : 4;
      break;
    case 'C':
      c.shape = "C";
      c.line = type.size() > 1 && type[1] >= '1' && type[1] <= '5' ? type[1] - '0' : 3;
      break;
    default:
      c.shape = "G";
      c.line = type == "G1" ? 1 : 2;
      break;
  }
  if (type.find("8vb") != std::string_view::npos) c.dis = 8;
  if (type.find("15mb") != std::string_view::npos) c.dis = 15;
  if (type.find("8va") != std::string_view::npos) {
    c.dis = 8;
    c.below = false;
  }
  if (type.find("15ma") != std::string_view::npos) {
    c.dis = 15;
    c.below = false;
  }
  return c;
}

void set_clef(pugi::xml_node clef, const ClefSpec& spec) {
  clef.append_attribute("shape") = spec.shape.c_str();
  clef.append_attribute("line") = spec.line;
  if (spec.dis != 0) {
    clef.append_attribute("dis") = spec.dis;
    clef.append_attribute("dis.place") = spec.below ? "below" : "above";
  }
}

/// MEI's key signature: "0", "3s" or "2f".
std::string key_sig(int accidentals) {
  if (accidentals == 0) return "0";
  return fmt::format("{}{}", accidentals < 0 ? -accidentals : accidentals,
                     accidentals < 0 ? 'f' : 's');
}

/// What a key signature does to one letter: +1, -1 or 0.
int key_alter(int accidentals, char letter) {
  constexpr std::string_view kSharps = "FCGDAEB";
  constexpr std::string_view kFlats = "BEADGCF";
  if (accidentals > 0) return kSharps.find(letter) < static_cast<std::size_t>(accidentals) ? 1 : 0;
  if (accidentals < 0) return kFlats.find(letter) < static_cast<std::size_t>(-accidentals) ? -1 : 0;
  return 0;
}

/// A drawn accidental, in MEI's terms.
const char* written_accid(int alter) {
  switch (alter) {
    case -3: return "tf";
    case -2: return "ff";
    case -1: return "f";
    case 1: return "s";
    case 2: return "x";
    case 3: return "ts";
    default: return "n";
  }
}

/// A sounding accidental, in MEI's terms.
const char* gestural_accid(int alter) {
  switch (alter) {
    case -3: return "tf";
    case -2: return "ff";
    case -1: return "f";
    case 1: return "s";
    case 2: return "ss";
    case 3: return "ts";
    default: return "n";
  }
}

/// MEI's articulation for a MuseScore articulation subtype, or "" for one MEI lacks.
std::string_view artic_name(std::string_view subtype) {
  for (const auto suffix : {"Above", "Below"}) {
    if (subtype.ends_with(suffix)) subtype.remove_suffix(std::string_view(suffix).size());
  }
  if (subtype == "articStaccato") return "stacc";
  if (subtype == "articStaccatissimo") return "stacciss";
  if (subtype == "articAccent") return "acc";
  if (subtype == "articTenuto") return "ten";
  if (subtype == "articMarcato") return "marc";
  if (subtype == "articAccentStaccato") return "acc stacc";
  if (subtype == "articTenutoStaccato") return "ten stacc";
  if (subtype == "articMarcatoStaccato") return "marc stacc";
  if (subtype == "articTenutoAccent") return "ten acc";
  return "";
}

/// Which notes of a measure on one staff show their accidental, and which.
///
/// Notes are visited in time order across voices: an accidental lasts to the end of the
/// bar, on its own line and octave, whichever voice wrote it. A note tied over from the
/// previous bar keeps its accidental silently, as engravers do.
std::unordered_map<const Note*, int> shown_accidentals(const score::Measure& measure, int key) {
  struct Ref {
    int onset;
    int voice;
    const Note* note;
  };
  std::vector<Ref> refs;
  for (std::size_t v = 0; v < measure.voices.size(); ++v) {
    for (const auto& el : measure.voices[v].elements) {
      for (const auto& note : el.notes) refs.push_back({el.onset, static_cast<int>(v), &note});
    }
  }
  std::stable_sort(refs.begin(), refs.end(), [](const Ref& a, const Ref& b) {
    return a.onset != b.onset ? a.onset < b.onset : a.voice < b.voice;
  });

  std::unordered_map<const Note*, int> shown;
  std::map<std::pair<char, int>, int> in_force;  // (letter, octave) -> alteration
  for (const auto& ref : refs) {
    const auto& pitch = ref.note->pitch;
    const char letter = score::tpc_letter(pitch.tpc);
    const auto line = std::make_pair(letter, score::written_octave(pitch));
    const int alter = score::tpc_alter(pitch.tpc);
    if (ref.note->tie_in) continue;

    const auto it = in_force.find(line);
    const int current = it != in_force.end() ? it->second : key_alter(key, letter);
    if (alter != current) {
      shown[ref.note] = alter;
      in_force[line] = alter;
    }
  }
  return shown;
}

/// What is in force at a measure of one staff: the clef, carried over from wherever it
/// was last declared. Worked out once per staff, as bars are asked for.
class ClefsInForce {
 public:
  const std::string& at(const score::Score& score, std::size_t staff, std::size_t measure) {
    auto& clefs = cache_[{&score, staff}];
    if (clefs.empty()) {
      const auto& s = score.staves[staff];
      std::string clef = s.default_clef;
      for (const auto& m : s.measures) {
        if (m.clef && !m.clef->concert.empty()) clef = m.clef->concert;
        clefs.push_back(clef);
      }
    }
    return clefs[std::min(measure, clefs.size() - 1)];
  }

 private:
  std::map<std::pair<const score::Score*, std::size_t>, std::vector<std::string>> cache_;
};

/// The time signature in force at each measure of a score, from its top staff.
score::TimeSig meter_at(const score::Score& score, std::size_t measure) {
  score::TimeSig meter;
  const auto& measures = score.staves.front().measures;
  for (std::size_t m = 0; m <= measure && m < measures.size(); ++m) {
    if (measures[m].time_sig) meter = *measures[m].time_sig;
  }
  return meter;
}

class Writer {
 public:
  Writer(const score::Score& layout, const std::vector<Bar>& bars) : layout_(layout), bars_(bars) {}

  std::string write() {
    pugi::xml_document doc;
    auto mei = doc.append_child("mei");
    mei.append_attribute("xmlns") = "http://www.music-encoding.org/ns/mei";
    mei.append_attribute("meiversion") = "5.0";

    auto title = mei.append_child("meiHead").append_child("fileDesc").append_child("titleStmt");
    title.append_child("title").text() = layout_.title().c_str();

    auto section = mei.append_child("music")
                       .append_child("body")
                       .append_child("mdiv")
                       .append_child("score");
    write_score_def(section.append_child("scoreDef"));
    section = section.append_child("section");

    for (std::size_t b = 0; b < bars_.size(); ++b) {
      if (b > 0) write_changes(section, bars_[b]);
      write_measure(section, bars_[b]);
    }

    std::ostringstream out;
    doc.save(out, "  ");
    return out.str();
  }

 private:
  [[nodiscard]] static int key_at(const Bar& bar) {
    const auto& measure = bar.score->staves.front().measures[static_cast<std::size_t>(bar.measure)];
    return bar.score->key_signature(measure.number).value_or(0);
  }

  /// The staff of the bar's score that output staff `s` is taken from, or nothing.
  [[nodiscard]] static const score::Staff* source(const Bar& bar, std::size_t s) {
    if (s >= bar.staves.size() || bar.staves[s] < 0) return nullptr;
    const auto index = static_cast<std::size_t>(bar.staves[s]);
    return index < bar.score->staves.size() ? &bar.score->staves[index] : nullptr;
  }

  /// The clef in force for output staff `s` in this bar, or "" if the bar leaves it empty.
  [[nodiscard]] std::string clef_at(const Bar& bar, std::size_t s) {
    if (source(bar, s) == nullptr) return "";
    return clefs_in_force_.at(*bar.score, static_cast<std::size_t>(bar.staves[s]),
                              static_cast<std::size_t>(bar.measure));
  }

  void write_score_def(pugi::xml_node def) {
    const Bar& first = bars_.front();
    key_ = key_at(first);
    def.append_child("keySig").append_attribute("sig") = key_sig(key_).c_str();

    meter_ = meter_at(*first.score, static_cast<std::size_t>(first.measure));
    auto meter = def.append_child("meterSig");
    meter.append_attribute("count") = meter_.n;
    meter.append_attribute("unit") = meter_.d;

    auto group = def.append_child("staffGrp");
    for (std::size_t s = 0; s < layout_.staves.size();) {
      // A part's staves, in a row: a piano's two, or a lone flute's one.
      const int part = layout_.staves[s].part_index;
      std::size_t end = s + 1;
      while (end < layout_.staves.size() && part >= 0 && layout_.staves[end].part_index == part) ++end;

      const auto* owner = layout_.part_for_staff(layout_.staves[s].id);
      const std::string label = owner != nullptr ? owner->display_name() : "";
      auto parent = group;
      if (end - s > 1) {
        parent = group.append_child("staffGrp");
        parent.append_attribute("symbol") = "brace";
        parent.append_attribute("bar.thru") = "true";
        if (!label.empty()) parent.append_child("label").text() = label.c_str();
      }
      for (std::size_t i = s; i < end; ++i) {
        auto staff_def = parent.append_child("staffDef");
        staff_def.append_attribute("n") = static_cast<int>(i + 1);
        staff_def.append_attribute("lines") = 5;
        if (end - s == 1 && !label.empty()) staff_def.append_child("label").text() = label.c_str();

        std::string clef = clef_at(first, i);
        if (clef.empty()) clef = layout_.staves[i].default_clef;
        clefs_.push_back(clef);
        set_clef(staff_def.append_child("clef"), clef_spec(clef));
      }
      s = end;
    }
  }

  /// A new key or time signature from this bar on, as a scoreDef before it.
  void write_changes(pugi::xml_node section, const Bar& bar) {
    const int key = key_at(bar);
    const auto meter = meter_at(*bar.score, static_cast<std::size_t>(bar.measure));
    const bool meter_changed = meter.n != meter_.n || meter.d != meter_.d;
    if (key == key_ && !meter_changed) return;

    auto def = section.append_child("scoreDef");
    if (key != key_) {
      key_ = key;
      def.append_child("keySig").append_attribute("sig") = key_sig(key).c_str();
    }
    if (meter_changed) {
      meter_ = meter;
      auto node = def.append_child("meterSig");
      node.append_attribute("count") = meter_.n;
      node.append_attribute("unit") = meter_.d;
    }
  }

  void write_measure(pugi::xml_node section, const Bar& bar) {
    const auto m = static_cast<std::size_t>(bar.measure);
    const auto& top = bar.score->staves.front().measures[m];
    division_ = bar.score->division;

    auto measure = section.append_child("measure");
    measure.append_attribute("n") = bar.number;
    if (!bar.label.empty()) measure.append_attribute("label") = bar.label.c_str();
    if (top.irregular) measure.append_attribute("metcon") = "false";
    if (!bar.right.empty()) measure.append_attribute("right") = bar.right.c_str();

    for (std::size_t s = 0; s < layout_.staves.size(); ++s) {
      auto staff = measure.append_child("staff");
      staff.append_attribute("n") = static_cast<int>(s + 1);

      const auto* from = source(bar, s);
      if (from == nullptr || m >= from->measures.size() || from->measures[m].voices.empty()) {
        auto layer = staff.append_child("layer");
        layer.append_attribute("n") = 1;
        layer.append_child("mSpace");
        continue;
      }
      const auto& here = from->measures[m];

      // A clef change takes effect at the start of the bar.
      std::optional<ClefSpec> clef_change;
      if (const std::string clef = clef_at(bar, s); !clef.empty() && clef != clefs_[s]) {
        clefs_[s] = clef;
        clef_change = clef_spec(clef);
      }

      const auto shown = shown_accidentals(here, key_);
      for (std::size_t v = 0; v < here.voices.size(); ++v) {
        auto layer = staff.append_child("layer");
        layer.append_attribute("n") = here.voices[v].index;
        if (v == 0 && clef_change) set_clef(layer.append_child("clef"), *clef_change);
        write_layer(layer, here.voices[v], shown);
      }
    }

    // Words over the top staff, such as which version a bar is from.
    if (!bar.text.empty()) {
      auto dir = measure.append_child("dir");
      dir.append_attribute("staff") = 1;
      dir.append_attribute("tstamp") = 1;
      dir.append_attribute("place") = "above";
      dir.text() = bar.text.c_str();
    }
  }

  void write_layer(pugi::xml_node layer, const Voice& voice,
                   const std::unordered_map<const Note*, int>& shown) {
    // Tuplets open around the element being written, outermost first.
    std::vector<std::pair<int, pugi::xml_node>> open;
    std::vector<int> open_ids;
    const auto container = [&] { return open.empty() ? layer : open.back().second; };
    int cursor = 0;

    for (const auto& el : voice.elements) {
      const auto chain = tuplet_chain(el, voice);
      const std::size_t common = shared_tuplets(open_ids, chain);
      open.resize(common);
      open_ids.resize(common);

      // A gap (a voice that starts late, or pauses) becomes invisible spaces, so the
      // notes after it keep their onsets.
      if (el.onset > cursor) {
        for (const auto& dur :
             space_values(written_gap(el.onset - cursor, open_ids, voice), division_)) {
          container().append_child("space").append_attribute("dur") = dur.c_str();
        }
      }

      for (std::size_t i = common; i < chain.size(); ++i) {
        const auto& tuplet = voice.tuplets[static_cast<std::size_t>(chain[i])];
        auto node = container().append_child("tuplet");
        node.append_attribute("num") = tuplet.actual;
        node.append_attribute("numbase") = tuplet.normal;
        open.emplace_back(chain[i], node);
        open_ids.push_back(chain[i]);
      }

      write_element(container(), el, voice, shown);
      cursor = std::max(cursor, el.end_tick());
    }
  }

  void write_element(pugi::xml_node parent, const Element& el, const Voice& voice,
                     const std::unordered_map<const Note*, int>& shown) {
    if (el.is_rest() && el.duration_type == "measure") {
      parent.append_child("mRest");
      return;
    }

    const Value value = element_value(el, voice, division_);
    const auto set_value = [&](pugi::xml_node node) {
      node.append_attribute("dur") = value.dur.c_str();
      if (value.dots > 0) node.append_attribute("dots") = value.dots;
    };

    if (el.is_rest() || el.notes.empty()) {
      auto node = parent.append_child(el.is_rest() ? "rest" : "space");
      set_value(node);
      return;
    }

    std::string artic;
    for (const auto& subtype : el.articulations) {
      const auto name = artic_name(subtype);
      if (name.empty()) continue;
      if (!artic.empty()) artic += ' ';
      artic += name;
    }

    pugi::xml_node holder = parent;
    if (el.notes.size() > 1) {
      holder = parent.append_child("chord");
      set_value(holder);
      if (!artic.empty()) holder.append_attribute("artic") = artic.c_str();
    }
    for (const auto& note : el.notes) {
      auto node = holder.append_child("note");
      if (el.notes.size() == 1) {
        set_value(node);
        if (!artic.empty()) node.append_attribute("artic") = artic.c_str();
      }
      write_pitch(node, note, shown);
    }
  }

  static void write_pitch(pugi::xml_node node, const Note& note,
                          const std::unordered_map<const Note*, int>& shown) {
    const char pname[2] = {
        static_cast<char>(std::tolower(static_cast<unsigned char>(score::tpc_letter(note.pitch.tpc)))),
        '\0'};
    node.append_attribute("pname") = pname;
    node.append_attribute("oct") = score::written_octave(note.pitch);

    const int alter = score::tpc_alter(note.pitch.tpc);
    if (const auto it = shown.find(&note); it != shown.end()) {
      node.append_attribute("accid") = written_accid(it->second);
    }
    // Always say what sounds: Verovio works out each note's pitch from this, not from the
    // key signature, and stave matches a diff's marks by that pitch.
    node.append_attribute("accid.ges") = gestural_accid(alter);

    if (note.tie_in && note.tie_out) {
      node.append_attribute("tie") = "m";
    } else if (note.tie_out) {
      node.append_attribute("tie") = "i";
    } else if (note.tie_in) {
      node.append_attribute("tie") = "t";
    }
  }

  const score::Score& layout_;
  const std::vector<Bar>& bars_;
  ClefsInForce clefs_in_force_;
  int division_ = 480;
  int key_ = 0;
  score::TimeSig meter_;
  std::vector<std::string> clefs_;  ///< each staff's clef in force, as MuseScore names it
};

}  // namespace

void Quarters::add(long long n, long long d) {
  num = num * d + n * den;
  den *= d;
  const long long g = std::gcd(num < 0 ? -num : num, den);
  if (g > 1) {
    num /= g;
    den /= g;
  }
}

std::string Quarters::text() const {
  return den == 1 ? fmt::format("{}", num) : fmt::format("{}/{}", num, den);
}

std::vector<Quarters> onsets(const score::Voice& voice, int division) {
  // Walks the voice as Writer::write_layer writes it, adding up what each written
  // element and space lasts.
  std::vector<Quarters> out;
  out.reserve(voice.elements.size());
  std::vector<int> open;
  Quarters at;
  int cursor = 0;

  for (const auto& el : voice.elements) {
    const auto chain = tuplet_chain(el, voice);
    open.resize(shared_tuplets(open, chain));

    if (el.onset > cursor) {
      for (const auto& dur : space_values(written_gap(el.onset - cursor, open, voice), division)) {
        const auto q = sounding(Value{dur, 0}, open, open.size(), voice);
        at.add(q.num, q.den);
      }
    }
    open = chain;
    out.push_back(at);

    if (el.is_rest() && el.duration_type == "measure") {
      at.add(el.ticks, division);  // a whole bar, which is always whole ticks
    } else {
      const auto q = sounding(element_value(el, voice, division), chain, chain.size(), voice);
      at.add(q.num, q.den);
    }
    cursor = std::max(cursor, el.end_tick());
  }
  return out;
}

std::string to_mei(const score::Score& score) {
  if (score.staves.empty() || score.staves.front().measures.empty()) return {};
  std::vector<int> staves(score.staves.size());
  for (std::size_t s = 0; s < staves.size(); ++s) staves[s] = static_cast<int>(s);
  std::vector<Bar> bars;
  const auto& measures = score.staves.front().measures;
  for (std::size_t m = 0; m < measures.size(); ++m) {
    Bar bar;
    bar.score = &score;
    bar.measure = static_cast<int>(m);
    bar.staves = staves;
    bar.number = measures[m].number;
    bars.push_back(std::move(bar));
  }
  return Writer(score, bars).write();
}

std::string to_mei(const score::Score& layout, const std::vector<Bar>& bars) {
  if (layout.staves.empty() || bars.empty()) return {};
  for (const auto& bar : bars) {
    if (bar.score == nullptr || bar.score->staves.empty() || bar.measure < 0 ||
        static_cast<std::size_t>(bar.measure) >= bar.score->staves.front().measures.size()) {
      return {};
    }
  }
  return Writer(layout, bars).write();
}

}  // namespace motif::mei
