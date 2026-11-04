#include "motif/render/stave_marks.h"

#include <fmt/format.h>

#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "motif/export/mei.h"

namespace motif::render {
namespace {

using diff::MeasureOp;
using diff::NoteChange;
using score::Element;
using score::Score;

enum class Tone { Added, Changed, Removed };

const char* tone_name(Tone tone) {
  switch (tone) {
    case Tone::Added: return "added";
    case Tone::Changed: return "changed";
    case Tone::Removed: return "removed";
  }
  return "changed";
}

/// Finds things in one version of the score the way the diff names them.
class Lookup {
 public:
  explicit Lookup(const Score& score) : score_(score) {}

  /// The staff's position from the top, counting from 1, or 0 if it isn't in the score.
  [[nodiscard]] int staff_number(int staff_id) const {
    for (std::size_t i = 0; i < score_.staves.size(); ++i) {
      if (score_.staves[i].id == staff_id) return static_cast<int>(i) + 1;
    }
    return 0;
  }

  /// The first measure numbered `number`, as an index, or -1.
  [[nodiscard]] int measure_index(int number) const {
    const auto& measures = score_.staves.front().measures;
    for (std::size_t i = 0; i < measures.size(); ++i) {
      if (measures[i].number == number) return static_cast<int>(i);
    }
    return -1;
  }

  [[nodiscard]] int measure_number(int index) const {
    return score_.staves.front().measures[static_cast<std::size_t>(index)].number;
  }

  [[nodiscard]] int measure_count() const {
    return static_cast<int>(score_.staves.front().measures.size());
  }

  struct Found {
    const Element* chord = nullptr;
    std::string at;  ///< when it starts, exactly, as stave reads it
  };

  /// The chord starting at `onset` in `voice` of measure `index` on staff `staff` (from 1).
  [[nodiscard]] Found chord(int staff, int index, int voice, int onset) const {
    if (staff < 1 || static_cast<std::size_t>(staff) > score_.staves.size() || index < 0) return {};
    const auto& measures = score_.staves[static_cast<std::size_t>(staff) - 1].measures;
    if (static_cast<std::size_t>(index) >= measures.size()) return {};
    for (const auto& v : measures[static_cast<std::size_t>(index)].voices) {
      if (v.index != voice) continue;
      for (std::size_t i = 0; i < v.elements.size(); ++i) {
        const auto& el = v.elements[i];
        if (el.onset != onset || !el.is_chord()) continue;
        return {&el, mei::onsets(v, score_.division)[i].text()};
      }
    }
    return {};
  }

 private:
  const Score& score_;
};

/// The marks for one version of the score, as stave's marks file will list them.
class SideMarks {
 public:
  explicit SideMarks(const Lookup& lookup) : lookup_(lookup) {}

  /// Marks a whole bar (by index) on one staff (from 1), or on every staff for 0.
  void bar(int staff, int measure, Tone tone) {
    if (measure >= 0 && measure < lookup_.measure_count()) bars_[{staff, measure}] = tone;
  }

  /// Marks the notes of the chord a change names: the one sounding `pitch`, or all of
  /// them for a change to the whole chord. Returns false when no such note exists.
  bool chord(int staff, int measure, int voice, int onset, std::optional<int> pitch, Tone tone) {
    const auto found = lookup_.chord(staff, measure, voice, onset);
    if (found.chord == nullptr) return false;
    bool any = false;
    for (const auto& note : found.chord->notes) {
      if (pitch && note.pitch.midi != *pitch) continue;
      const NoteKey key{staff, measure, voice, onset, note.pitch.midi};
      const auto [it, inserted] = notes_.emplace(key, Note{tone, found.at});
      // A new note reads as added, even when the diff also calls its chord changed (as
      // when it replaces a rest, which makes the rhythm look edited too).
      if (!inserted && tone == Tone::Added) it->second.tone = Tone::Added;
      any = true;
    }
    return any;
  }

  [[nodiscard]] bool bar_marked(int staff, int measure) const {
    return bars_.count({0, measure}) > 0 || bars_.count({staff, measure}) > 0;
  }

  [[nodiscard]] bool notes_marked(int staff, int measure) const {
    for (const auto& [key, note] : notes_) {
      if (std::get<0>(key) == staff && std::get<1>(key) == measure) return true;
    }
    return false;
  }

  /// stave won't colour a note in a bar that is itself marked: the bar's colour wins.
  void drop_notes_in_marked_bars() {
    for (auto it = notes_.begin(); it != notes_.end();) {
      if (bar_marked(std::get<0>(it->first), std::get<1>(it->first))) {
        it = notes_.erase(it);
      } else {
        ++it;
      }
    }
  }

  /// The earliest marked bar, as (measure index, staff), taking the topmost staff there.
  [[nodiscard]] std::optional<std::pair<int, int>> first() const {
    std::optional<std::pair<int, int>> out;
    const auto consider = [&](int measure, int staff) {
      const std::pair<int, int> here{measure, staff == 0 ? 1 : staff};
      if (!out || here < *out) out = here;
    };
    for (const auto& [key, tone] : bars_) consider(key.second, key.first);
    for (const auto& [key, note] : notes_) consider(std::get<1>(key), std::get<0>(key));
    return out;
  }

  /// The marks as stave's JSON, whole bars first, as runs where they can be. `count` is
  /// set to how many marks it lists.
  [[nodiscard]] std::string json(int& count) const {
    std::vector<std::string> items;
    for (auto it = bars_.begin(); it != bars_.end();) {
      const auto [staff, start] = it->first;
      const Tone tone = it->second;
      int end = start;
      auto next = std::next(it);
      while (next != bars_.end() && next->first.first == staff && next->first.second == end + 1 &&
             next->second == tone &&
             lookup_.measure_number(end + 1) == lookup_.measure_number(end) + 1) {
        ++end;
        ++next;
      }
      std::string item = fmt::format(R"({{"tone": "{}", "bar": {})", tone_name(tone),
                                     lookup_.measure_number(start));
      if (end != start) item += fmt::format(R"(, "to": {})", lookup_.measure_number(end));
      if (staff != 0) item += fmt::format(R"(, "staff": {})", staff);
      item += "}";
      items.push_back(std::move(item));
      it = next;
    }
    for (const auto& [key, note] : notes_) {
      const auto [staff, measure, voice, onset, pitch] = key;
      (void)onset;
      items.push_back(fmt::format(
          R"({{"tone": "{}", "bar": {}, "staff": {}, "voice": {}, "at": "{}", "pitch": {}}})",
          tone_name(note.tone), lookup_.measure_number(measure), staff, voice, note.at, pitch));
    }

    count = static_cast<int>(items.size());
    std::string out = "[\n";
    for (std::size_t i = 0; i < items.size(); ++i) {
      out += "  " + items[i] + (i + 1 < items.size() ? ",\n" : "\n");
    }
    return out + "]\n";
  }

 private:
  /// One note of the score: staff (from 1), measure index, voice, onset and pitch.
  using NoteKey = std::tuple<int, int, int, int, int>;
  struct Note {
    Tone tone;
    std::string at;
  };

  const Lookup& lookup_;
  std::map<NoteKey, Note> notes_;
  std::map<std::pair<int, int>, Tone> bars_;  ///< (staff, measure index); staff 0 is every staff
};

/// Marks every bar of an instrument's staves.
void mark_part(SideMarks& side, const Lookup& lookup, const score::Part& part, Tone tone) {
  for (const int staff_id : part.staff_ids) {
    const int staff = lookup.staff_number(staff_id);
    if (staff == 0) continue;
    for (int m = 0; m < lookup.measure_count(); ++m) side.bar(staff, m, tone);
  }
}

bool whole_chord(const NoteChange& change) {
  return change.kind == NoteChange::Kind::DurationChanged ||
         change.kind == NoteChange::Kind::ArticulationChanged;
}

std::optional<int> after_pitch(const NoteChange& change) {
  if (whole_chord(change) || !change.after) return std::nullopt;
  return change.after->midi;
}

std::optional<int> before_pitch(const NoteChange& change) {
  if (whole_chord(change) || !change.before) return std::nullopt;
  return change.before->midi;
}

/// Each measure's partner in the other version, from the alignment, or -1.
struct MeasureMap {
  std::vector<int> after_to_before;
  std::vector<int> before_to_after;
};

MeasureMap measure_map(const diff::Alignment& alignment, int before_count, int after_count) {
  MeasureMap map{std::vector<int>(static_cast<std::size_t>(after_count), -1),
                 std::vector<int>(static_cast<std::size_t>(before_count), -1)};
  for (const auto& link : alignment) {
    if (link.op != MeasureOp::Match || link.before < 0 || link.after < 0) continue;
    if (link.before >= before_count || link.after >= after_count) continue;
    map.after_to_before[static_cast<std::size_t>(link.after)] = link.before;
    map.before_to_after[static_cast<std::size_t>(link.before)] = link.after;
  }
  return map;
}

/// stave's --pairs: runs of matched bars, by their printed numbers.
std::string pairs_text(const diff::Alignment& alignment, const Lookup& before, const Lookup& after) {
  std::vector<std::pair<int, int>> matched;
  for (const auto& link : alignment) {
    if (link.op == MeasureOp::Match && link.before >= 0 && link.after >= 0 &&
        link.before < before.measure_count() && link.after < after.measure_count()) {
      matched.emplace_back(link.before, link.after);
    }
  }

  const auto range = [](int first, int last) {
    return first == last ? fmt::format("{}", first) : fmt::format("{}-{}", first, last);
  };
  std::string out;
  for (std::size_t i = 0; i < matched.size();) {
    std::size_t j = i;
    while (j + 1 < matched.size() && matched[j + 1].first == matched[j].first + 1 &&
           matched[j + 1].second == matched[j].second + 1 &&
           before.measure_number(matched[j + 1].first) == before.measure_number(matched[j].first) + 1 &&
           after.measure_number(matched[j + 1].second) == after.measure_number(matched[j].second) + 1) {
      ++j;
    }
    if (!out.empty()) out += ',';
    out += range(before.measure_number(matched[i].first), before.measure_number(matched[j].first));
    out += '=';
    out += range(after.measure_number(matched[i].second), after.measure_number(matched[j].second));
    i = j + 1;
  }
  return out;
}

}  // namespace

StaveMarks stave_marks(const diff::ScoreDiff& diff, const Score& after) {
  StaveMarks out;
  if (after.staves.empty() || after.staves.front().measures.empty()) {
    out.json = "[\n]\n";
    return out;
  }

  const Lookup lookup(after);
  SideMarks marks(lookup);
  std::set<std::pair<int, int>> lost_notes;  // (staff, measure index): bars where notes were removed

  for (const int index : diff.inserted_measures) marks.bar(0, index, Tone::Added);
  for (const auto& change : diff.orchestration.changes) {
    if (change.kind == diff::PartChange::Kind::Added && change.part_index >= 0 &&
        static_cast<std::size_t>(change.part_index) < after.parts.size()) {
      mark_part(marks, lookup, after.parts[static_cast<std::size_t>(change.part_index)], Tone::Added);
    }
  }

  for (const auto& run : diff.staves) {
    const int staff = lookup.staff_number(run.staff_id);
    if (staff == 0) continue;
    for (const auto& change : run.notes) {
      // A whole inserted bar is marked above; a whole deleted one has nothing left to mark.
      if (change.whole_measure) continue;
      const int m = lookup.measure_index(change.measure);
      if (change.kind == NoteChange::Kind::Removed) {
        if (m >= 0) lost_notes.insert({staff, m});
        continue;
      }
      const Tone tone = change.kind == NoteChange::Kind::Added ? Tone::Added : Tone::Changed;
      if (!marks.chord(staff, m, change.voice, change.onset, after_pitch(change), tone)) {
        ++out.dropped;
      }
    }
  }
  marks.drop_notes_in_marked_bars();

  // A bar that lost notes turns red, unless something in it is marked another way.
  for (const auto& [staff, m] : lost_notes) {
    if (!marks.notes_marked(staff, m) && !marks.bar_marked(staff, m)) {
      marks.bar(staff, m, Tone::Removed);
    }
  }

  if (const auto first = marks.first()) {
    out.start_bar = lookup.measure_number(first->first);
    out.start_staff = first->second;
  }
  out.json = marks.json(out.marks);
  return out;
}

StaveComparison stave_comparison(const diff::ScoreDiff& diff, const Score& before,
                                 const Score& after) {
  StaveComparison out;
  if (before.staves.empty() || before.staves.front().measures.empty() || after.staves.empty() ||
      after.staves.front().measures.empty()) {
    out.before_json = out.after_json = "[\n]\n";
    return out;
  }

  const Lookup old_lookup(before);
  const Lookup new_lookup(after);
  SideMarks old_marks(old_lookup);
  SideMarks new_marks(new_lookup);
  const auto map =
      measure_map(diff.alignment, old_lookup.measure_count(), new_lookup.measure_count());

  // Bars inserted and deleted, on every staff.
  for (const int index : diff.inserted_measures) new_marks.bar(0, index, Tone::Added);
  for (const auto& link : diff.alignment) {
    if (link.op == MeasureOp::Delete) old_marks.bar(0, link.before, Tone::Removed);
  }

  // Instruments added and removed, all the way through.
  for (const auto& change : diff.orchestration.changes) {
    if (change.part_index < 0) continue;
    const auto index = static_cast<std::size_t>(change.part_index);
    if (change.kind == diff::PartChange::Kind::Added && index < after.parts.size()) {
      mark_part(new_marks, new_lookup, after.parts[index], Tone::Added);
    } else if (change.kind == diff::PartChange::Kind::Removed && index < before.parts.size()) {
      mark_part(old_marks, old_lookup, before.parts[index], Tone::Removed);
    }
  }

  for (const auto& run : diff.staves) {
    const int new_staff = new_lookup.staff_number(run.staff_id);
    const int old_staff = old_lookup.staff_number(run.before_staff_id);
    for (const auto& change : run.notes) {
      if (change.whole_measure) continue;  // its bar is marked whole, above

      // A change's measure is numbered in the newer score; its partner in the older is
      // where the old notes are.
      const int new_m = new_lookup.measure_index(change.measure);
      const int old_m = new_m >= 0 ? map.after_to_before[static_cast<std::size_t>(new_m)] : -1;

      bool found = true;
      switch (change.kind) {
        case NoteChange::Kind::Added:
          found = new_marks.chord(new_staff, new_m, change.voice, change.onset, after_pitch(change),
                                  Tone::Added);
          break;
        case NoteChange::Kind::Removed:
          found = old_marks.chord(old_staff, old_m, change.voice, change.before_onset,
                                  before_pitch(change), Tone::Removed);
          break;
        case NoteChange::Kind::PitchChanged:
        case NoteChange::Kind::DurationChanged:
        case NoteChange::Kind::ArticulationChanged: {
          const bool now = new_marks.chord(new_staff, new_m, change.voice, change.onset,
                                           after_pitch(change), Tone::Changed);
          const bool was = old_marks.chord(old_staff, old_m, change.voice, change.before_onset,
                                           before_pitch(change), Tone::Changed);
          found = now && was;
          break;
        }
      }
      if (!found) ++out.dropped;
    }
  }
  old_marks.drop_notes_in_marked_bars();
  new_marks.drop_notes_in_marked_bars();

  // Open on the earliest change on either side, in the newer score's bars. A change only
  // in the older score (a deleted bar) opens where that bar would have been.
  const auto to_new = [&](int old_m) {
    for (auto i = static_cast<std::size_t>(old_m); i < map.before_to_after.size(); ++i) {
      if (map.before_to_after[i] >= 0) return map.before_to_after[i];
    }
    return new_lookup.measure_count() - 1;
  };
  std::optional<std::pair<int, int>> first = new_marks.first();
  if (const auto old_first = old_marks.first()) {
    const std::pair<int, int> mapped{to_new(old_first->first), old_first->second};
    if (!first || mapped < *first) first = mapped;
  }
  if (first) {
    out.start_bar = new_lookup.measure_number(first->first);
    out.start_staff = first->second;
  }

  int old_count = 0;
  int new_count = 0;
  out.before_json = old_marks.json(old_count);
  out.after_json = new_marks.json(new_count);
  out.marks = old_count + new_count;
  out.pairs = pairs_text(diff.alignment, old_lookup, new_lookup);
  return out;
}

}  // namespace motif::render
