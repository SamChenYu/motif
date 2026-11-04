#include "motif/render/stave_marks.h"

#include <fmt/format.h>

#include "motif/export/mei.h"

#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace motif::render {
namespace {

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

/// One note of the newer score: staff (from 1), measure index, voice, onset and pitch.
using NoteKey = std::tuple<int, int, int, int, int>;
/// Bars of one staff (from 1; 0 for every staff), by measure index.
using BarKey = std::pair<int, int>;

/// Finds things in the newer score the way the diff names them.
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
    int measure = -1;     ///< the index of the measure it is in
    std::string at;       ///< when it starts, exactly, as stave reads it
  };

  /// The chord starting at `onset` in `voice` of bar `number` on staff `staff` (from 1).
  /// Bar numbers can repeat, so each measure of that number is tried.
  [[nodiscard]] Found chord(int staff, int number, int voice, int onset) const {
    const auto& measures = score_.staves[static_cast<std::size_t>(staff) - 1].measures;
    for (std::size_t m = 0; m < measures.size(); ++m) {
      if (measures[m].number != number) continue;
      for (const auto& v : measures[m].voices) {
        if (v.index != voice) continue;
        for (std::size_t i = 0; i < v.elements.size(); ++i) {
          const auto& el = v.elements[i];
          if (el.onset != onset || !el.is_chord()) continue;
          return {&el, static_cast<int>(m), mei::onsets(v, score_.division)[i].text()};
        }
      }
    }
    return {};
  }

 private:
  const Score& score_;
};

}  // namespace

StaveMarks stave_marks(const diff::ScoreDiff& diff, const Score& after) {
  StaveMarks out;
  if (after.staves.empty() || after.staves.front().measures.empty()) {
    out.json = "[]\n";
    return out;
  }

  const Lookup lookup(after);
  std::map<NoteKey, Tone> notes;
  std::map<NoteKey, std::string> note_at;  // each marked note's start, as stave reads it
  std::map<BarKey, Tone> bars;
  std::set<BarKey> lost_notes;  // bars where notes were removed

  // Inserted bars, on every staff.
  for (const int index : diff.inserted_measures) {
    if (index >= 0 && index < lookup.measure_count()) bars[{0, index}] = Tone::Added;
  }

  // Added instruments, all the way through.
  for (const auto& change : diff.orchestration.changes) {
    if (change.kind != diff::PartChange::Kind::Added || change.part_index < 0 ||
        static_cast<std::size_t>(change.part_index) >= after.parts.size()) {
      continue;
    }
    for (const int staff_id : after.parts[static_cast<std::size_t>(change.part_index)].staff_ids) {
      const int staff = lookup.staff_number(staff_id);
      if (staff == 0) continue;
      for (int m = 0; m < lookup.measure_count(); ++m) bars[{staff, m}] = Tone::Added;
    }
  }

  const auto mark_note = [&](int staff, int measure, const NoteChange& change,
                             const std::string& at, int pitch, Tone tone) {
    const NoteKey key{staff, measure, change.voice, change.onset, pitch};
    note_at[key] = at;
    const auto [it, inserted] = notes.emplace(key, tone);
    // A new note reads as added, even when the diff also calls its chord changed (as
    // when it replaces a rest, which makes the rhythm look edited too).
    if (!inserted && tone == Tone::Added) it->second = Tone::Added;
  };

  for (const auto& run : diff.staves) {
    const int staff = lookup.staff_number(run.staff_id);
    if (staff == 0) continue;

    for (const auto& change : run.notes) {
      if (change.kind == NoteChange::Kind::Removed) {
        // A deleted bar's number is from the older score; there is nothing to mark.
        if (change.whole_measure) continue;
        const int m = lookup.measure_index(change.measure);
        if (m >= 0) lost_notes.insert({staff, m});
        continue;
      }
      if (change.whole_measure) continue;  // its bar is marked whole, above

      const auto [chord, m, at] = lookup.chord(staff, change.measure, change.voice, change.onset);
      if (chord == nullptr) {
        ++out.dropped;
        continue;
      }

      const bool whole_chord = change.kind == NoteChange::Kind::DurationChanged ||
                               change.kind == NoteChange::Kind::ArticulationChanged;
      const Tone tone =
          change.kind == NoteChange::Kind::Added ? Tone::Added : Tone::Changed;
      bool found = false;
      for (const auto& note : chord->notes) {
        if (!whole_chord && (!change.after || note.pitch.midi != change.after->midi)) continue;
        mark_note(staff, m, change, at, note.pitch.midi, tone);
        found = true;
      }
      if (!found) ++out.dropped;
    }
  }

  // stave won't colour a note in a bar that is itself marked: the bar's colour wins.
  for (auto it = notes.begin(); it != notes.end();) {
    const auto [staff, m, voice, onset, pitch] = it->first;
    if (bars.count({0, m}) > 0 || bars.count({staff, m}) > 0) {
      it = notes.erase(it);
    } else {
      ++it;
    }
  }

  // A bar that lost notes turns red, unless something in it is marked another way.
  std::set<BarKey> noted;
  for (const auto& [key, tone] : notes) noted.insert({std::get<0>(key), std::get<1>(key)});
  for (const auto& [staff, m] : lost_notes) {
    if (noted.count({staff, m}) > 0 || bars.count({0, m}) > 0 || bars.count({staff, m}) > 0) {
      continue;
    }
    bars[{staff, m}] = Tone::Removed;
  }

  // Where to start: the earliest marked bar, on the topmost staff marked there.
  std::optional<std::pair<int, int>> first;  // (measure index, staff)
  const auto consider = [&](int m, int staff) {
    const std::pair<int, int> here{m, staff == 0 ? 1 : staff};
    if (!first || here < *first) first = here;
  };
  for (const auto& [key, tone] : bars) consider(key.second, key.first);
  for (const auto& [key, tone] : notes) consider(std::get<1>(key), std::get<0>(key));
  if (first) {
    out.start_bar = lookup.measure_number(first->first);
    out.start_staff = first->second;
  }

  std::vector<std::string> items;

  // Whole bars, as runs: "bars 12 to 15 on staff 2" is one mark, not four.
  for (auto it = bars.begin(); it != bars.end();) {
    const auto [staff, start] = it->first;
    const Tone tone = it->second;
    int end = start;
    auto next = std::next(it);
    while (next != bars.end() && next->first.first == staff && next->first.second == end + 1 &&
           next->second == tone &&
           lookup.measure_number(end + 1) == lookup.measure_number(end) + 1) {
      ++end;
      ++next;
    }
    std::string item = fmt::format(R"({{"tone": "{}", "bar": {})", tone_name(tone),
                                   lookup.measure_number(start));
    if (end != start) item += fmt::format(R"(, "to": {})", lookup.measure_number(end));
    if (staff != 0) item += fmt::format(R"(, "staff": {})", staff);
    item += "}";
    items.push_back(std::move(item));
    it = next;
  }

  for (const auto& [key, tone] : notes) {
    const auto [staff, m, voice, onset, pitch] = key;
    items.push_back(fmt::format(
        R"({{"tone": "{}", "bar": {}, "staff": {}, "voice": {}, "at": "{}", "pitch": {}}})",
        tone_name(tone), lookup.measure_number(m), staff, voice, note_at.at(key),
        pitch));
  }

  out.marks = static_cast<int>(items.size());
  out.json = "[\n";
  for (std::size_t i = 0; i < items.size(); ++i) {
    out.json += "  " + items[i] + (i + 1 < items.size() ? ",\n" : "\n");
  }
  out.json += "]\n";
  return out;
}

}  // namespace motif::render
