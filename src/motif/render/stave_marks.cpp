#include "motif/render/stave_marks.h"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "motif/diff/align.h"
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

/// A chord the diff names, found in a score stave will show.
struct Found {
  const Element* chord = nullptr;
  std::string at;  ///< when it starts, exactly, as stave reads it
};

/// The chord starting at `onset` in `voice` of measure `measure` of staff `staff` (an index).
Found find_chord(const Score& score, std::size_t staff, int measure, int voice, int onset) {
  if (staff >= score.staves.size() || measure < 0) return {};
  const auto& measures = score.staves[staff].measures;
  if (static_cast<std::size_t>(measure) >= measures.size()) return {};
  for (const auto& v : measures[static_cast<std::size_t>(measure)].voices) {
    if (v.index != voice) continue;
    for (std::size_t i = 0; i < v.elements.size(); ++i) {
      const auto& el = v.elements[i];
      if (el.onset != onset || !el.is_chord()) continue;
      return {&el, mei::onsets(v, score.division)[i].text()};
    }
  }
  return {};
}

/// The bars of a score stave will show, as its marks name them.
class Bars {
 public:
  virtual ~Bars() = default;
  [[nodiscard]] virtual int count() const = 0;
  [[nodiscard]] virtual int number(int bar) const = 0;  ///< what the marks call bar `bar`
  /// The chord starting at `onset` in `voice` of bar `bar` (an index) on staff `staff`
  /// (counting from 1).
  [[nodiscard]] virtual Found chord(int staff, int bar, int voice, int onset) const = 0;
};

/// Finds things in one version of the score the way the diff names them.
class Lookup : public Bars {
 public:
  explicit Lookup(const Score& score) : score_(score) {}

  [[nodiscard]] int count() const override { return measure_count(); }
  [[nodiscard]] int number(int bar) const override { return measure_number(bar); }
  [[nodiscard]] Found chord(int staff, int bar, int voice, int onset) const override {
    if (staff < 1) return {};
    return find_chord(score_, static_cast<std::size_t>(staff) - 1, bar, voice, onset);
  }

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

 private:
  const Score& score_;
};

/// The bars of a unified diff: each taken from one version or the other.
class UnifiedBars : public Bars {
 public:
  explicit UnifiedBars(const std::vector<mei::Bar>& bars) : bars_(bars) {}

  [[nodiscard]] int count() const override { return static_cast<int>(bars_.size()); }
  [[nodiscard]] int number(int bar) const override {
    return bars_[static_cast<std::size_t>(bar)].number;
  }
  [[nodiscard]] Found chord(int staff, int bar, int voice, int onset) const override {
    if (staff < 1 || bar < 0 || bar >= count()) return {};
    const auto& b = bars_[static_cast<std::size_t>(bar)];
    const auto s = static_cast<std::size_t>(staff) - 1;
    if (s >= b.staves.size() || b.staves[s] < 0) return {};
    return find_chord(*b.score, static_cast<std::size_t>(b.staves[s]), b.measure, voice, onset);
  }

 private:
  const std::vector<mei::Bar>& bars_;
};

/// The marks for one score stave will show, as its marks file will list them.
class SideMarks {
 public:
  explicit SideMarks(const Bars& lookup) : lookup_(lookup) {}

  /// Marks a whole bar (by index) on one staff (from 1), or on every staff for 0.
  void bar(int staff, int measure, Tone tone) {
    if (measure >= 0 && measure < lookup_.count()) bars_[{staff, measure}] = tone;
  }

  /// Tints a whole bar's background (by index), on every staff: a diff's line highlight.
  /// Notes and bars marked in it still show their own colour on top.
  void fill(int measure, Tone tone) {
    if (measure >= 0 && measure < lookup_.count()) fills_[{0, measure}] = tone;
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
    const auto runs = [&](const std::map<std::pair<int, int>, Tone>& marked, bool fill) {
      for (auto it = marked.begin(); it != marked.end();) {
        const auto [staff, start] = it->first;
        const Tone tone = it->second;
        int end = start;
        auto next = std::next(it);
        while (next != marked.end() && next->first.first == staff &&
               next->first.second == end + 1 && next->second == tone &&
               lookup_.number(end + 1) == lookup_.number(end) + 1) {
          ++end;
          ++next;
        }
        std::string item = fmt::format(R"({{"tone": "{}", "bar": {})", tone_name(tone),
                                       lookup_.number(start));
        if (end != start) item += fmt::format(R"(, "to": {})", lookup_.number(end));
        if (staff != 0) item += fmt::format(R"(, "staff": {})", staff);
        if (fill) item += R"(, "fill": true)";
        item += "}";
        items.push_back(std::move(item));
        it = next;
      }
    };
    runs(fills_, true);
    runs(bars_, false);
    for (const auto& [key, note] : notes_) {
      const auto [staff, measure, voice, onset, pitch] = key;
      (void)onset;
      items.push_back(fmt::format(
          R"({{"tone": "{}", "bar": {}, "staff": {}, "voice": {}, "at": "{}", "pitch": {}}})",
          tone_name(note.tone), lookup_.number(measure), staff, voice, note.at, pitch));
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

  const Bars& lookup_;
  std::map<NoteKey, Note> notes_;
  std::map<std::pair<int, int>, Tone> bars_;  ///< (staff, measure index); staff 0 is every staff
  std::map<std::pair<int, int>, Tone> fills_;  ///< the same, for tinted backgrounds
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

UnifiedDiff unified_diff(const diff::ScoreDiff& diff, const Score& before, const Score& after,
                         const std::string& before_text, const std::string& after_text) {
  UnifiedDiff out;
  if (before.staves.empty() || before.staves.front().measures.empty() || after.staves.empty() ||
      after.staves.front().measures.empty() || diff.alignment.empty()) {
    return out;
  }
  const Lookup old_lookup(before);
  const Lookup new_lookup(after);

  // The score is laid out on the newer version's staves. Each takes its old bars from the
  // same instrument's staff in the older version; an added instrument has none.
  std::vector<int> new_staves(after.staves.size());
  std::vector<int> old_staves(after.staves.size(), -1);
  for (std::size_t s = 0; s < after.staves.size(); ++s) new_staves[s] = static_cast<int>(s);
  for (const auto& [old_id, new_id] : diff.staff_pairs) {
    const int n = new_lookup.staff_number(new_id);
    const int o = old_lookup.staff_number(old_id);
    if (n > 0 && o > 0) old_staves[static_cast<std::size_t>(n) - 1] = o - 1;
  }

  // Whether a matched bar differs on any instrument both versions have.
  const auto differs = [&](const diff::MeasureLink& link) {
    if (link.op != MeasureOp::Match) return true;
    for (std::size_t s = 0; s < after.staves.size(); ++s) {
      if (old_staves[s] < 0) continue;
      const auto& was = before.staves[static_cast<std::size_t>(old_staves[s])].measures;
      const auto& now = after.staves[s].measures;
      const auto b = static_cast<std::size_t>(link.before);
      const auto a = static_cast<std::size_t>(link.after);
      if (b >= was.size() || a >= now.size()) return true;
      if (diff::measures_differ(was[b], now[a])) return true;
    }
    return false;
  };

  std::vector<mei::Bar> bars;
  std::vector<int> old_bar(static_cast<std::size_t>(old_lookup.measure_count()), -1);  // -> output bar
  std::vector<int> new_bar(static_cast<std::size_t>(new_lookup.measure_count()), -1);
  std::vector<int> after_to_before(static_cast<std::size_t>(new_lookup.measure_count()), -1);
  std::vector<int> deleted;   // output bars
  std::vector<int> inserted;  // output bars
  std::vector<bool> from_new;  // for each output bar
  std::vector<int> was_bars;   // output bars showing a changed stretch as it was
  std::vector<int> now_bars;   // ...and as it is

  const auto add = [&](const Score& score, int measure, const std::vector<int>& staves,
                       const std::string& text) {
    mei::Bar bar;
    bar.score = &score;
    bar.measure = measure;
    bar.staves = staves;
    bar.number = static_cast<int>(bars.size()) + 1;
    bar.label = std::to_string(score.staves.front().measures[static_cast<std::size_t>(measure)].number);
    bar.text = text;
    bars.push_back(std::move(bar));
    from_new.push_back(&score == &after);
    return static_cast<int>(bars.size()) - 1;
  };

  const auto& links = diff.alignment;
  for (std::size_t i = 0; i < links.size();) {
    if (!differs(links[i])) {
      new_bar[static_cast<std::size_t>(links[i].after)] = add(after, links[i].after, new_staves, "");
      after_to_before[static_cast<std::size_t>(links[i].after)] = links[i].before;
      ++i;
      continue;
    }

    // A changed stretch: every bar up to the next one that's the same in both. Like a
    // unified diff's hunk, the old bars come first, then the new.
    std::vector<const diff::MeasureLink*> hunk;
    for (; i < links.size() && differs(links[i]); ++i) hunk.push_back(&links[i]);
    ++out.hunks;

    bool first = true;
    for (const auto* link : hunk) {
      if (link->op == MeasureOp::Insert) continue;
      const int b = add(before, link->before, old_staves, first ? before_text : "");
      was_bars.push_back(b);
      old_bar[static_cast<std::size_t>(link->before)] = b;
      if (link->op == MeasureOp::Delete) deleted.push_back(b);
      first = false;
    }
    const bool had_old = !first;
    first = true;
    for (const auto* link : hunk) {
      if (link->op == MeasureOp::Delete) continue;
      if (first && had_old) bars.back().dashed_end = true;  // between the old bars and the new
      const int a = add(after, link->after, new_staves, first ? after_text : "");
      now_bars.push_back(a);
      new_bar[static_cast<std::size_t>(link->after)] = a;
      if (link->op == MeasureOp::Insert) inserted.push_back(a);
      if (link->op == MeasureOp::Match) after_to_before[static_cast<std::size_t>(link->after)] = link->before;
      first = false;
    }
  }

  const UnifiedBars unified(bars);
  SideMarks marks(unified);
  for (const int b : deleted) marks.bar(0, b, Tone::Removed);
  for (const int a : inserted) marks.bar(0, a, Tone::Added);
  // Like a unified diff's line highlights: the old stretch on red, the new on green.
  for (const int b : was_bars) marks.fill(b, Tone::Removed);
  for (const int a : now_bars) marks.fill(a, Tone::Added);

  // An added instrument is green wherever the newer version's bars are.
  for (std::size_t s = 0; s < after.staves.size(); ++s) {
    if (old_staves[s] >= 0) continue;
    for (std::size_t b = 0; b < bars.size(); ++b) {
      if (from_new[b]) marks.bar(static_cast<int>(s) + 1, static_cast<int>(b), Tone::Added);
    }
  }

  for (const auto& run : diff.staves) {
    const int staff = new_lookup.staff_number(run.staff_id);
    if (staff == 0) continue;
    for (const auto& change : run.notes) {
      if (change.whole_measure) continue;  // its bar is marked whole, above
      const int a = new_lookup.measure_index(change.measure);
      if (a < 0) continue;
      const int b = after_to_before[static_cast<std::size_t>(a)];
      const int was = b >= 0 ? old_bar[static_cast<std::size_t>(b)] : -1;
      const int now = new_bar[static_cast<std::size_t>(a)];

      bool found = true;
      if (change.kind != NoteChange::Kind::Removed) {
        found = now >= 0 && marks.chord(staff, now, change.voice, change.onset, after_pitch(change),
                                        Tone::Added);
      }
      if (change.kind != NoteChange::Kind::Added) {
        found = was >= 0 &&
                marks.chord(staff, was, change.voice, change.before_onset, before_pitch(change),
                            Tone::Removed) &&
                found;
      }
      if (!found) ++out.dropped;
    }
  }
  marks.drop_notes_in_marked_bars();

  // Open a bar before the first change, so it's seen coming in.
  if (const auto first = marks.first()) {
    out.start_bar = unified.number(std::max(0, first->first - 1));
    out.start_staff = first->second;
  }
  out.json = marks.json(out.marks);
  out.mei = mei::to_mei(after, bars);
  return out;
}

}  // namespace motif::render
