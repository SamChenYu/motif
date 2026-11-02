#include "motif/diff/engine.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "motif/analysis/transform.h"
#include "motif/diff/align.h"
#include "motif/diff/note_diff.h"

namespace motif::diff {
namespace {

using score::Measure;
using score::Score;
using score::Staff;

/// Longest before/after note sequence a run will carry for display. A longer run is
/// shown from the start and marked as elided -- printing a hundred note names helps
/// nobody, but silently dropping them would misrepresent the change.
constexpr std::size_t kMaxSequenceNotes = 32;

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool contains_ignoring_case(const std::string& haystack, const std::string& needle) {
  return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

/// How well two parts correspond. Higher is a stronger match; 0 means unrelated.
int part_affinity(const score::Part& before, const score::Part& after) {
  const bool same_instrument =
      !before.instrument_id.empty() && before.instrument_id == after.instrument_id;
  const bool same_name = before.display_name() == after.display_name();
  const bool same_id = !before.id.empty() && before.id == after.id;

  if (same_instrument && same_name) return 4;
  if (same_instrument && same_id) return 3;
  if (same_instrument) return 2;
  if (same_name) return 1;
  return 0;
}

/// Labels a staff, disambiguating the staves of a multi-staff part.
std::string staff_label(const score::Part* part, const Staff& staff) {
  if (part == nullptr) return fmt::format("Staff {}", staff.id);
  if (part->staff_ids.size() <= 1) return part->display_name();
  return fmt::format("{} · staff {}", part->display_name(), staff.index_in_part + 1);
}

/// Collects a run's notes in playing order, for before/after display.
///
/// Only single-voice material is collected: a run with independent voices has no single
/// linear reading, and printing one would misrepresent it.
StaffChange::SequenceState collect_sequence(const Staff& staff, int first_index, int last_index,
                                            std::vector<score::Note>& notes,
                                            std::vector<int>& ticks,
                                            std::vector<int>& measure_numbers,
                                            std::vector<int>& chord_ids) {
  auto state = StaffChange::SequenceState::Complete;
  int chord_id = 0;

  for (int i = first_index; i <= last_index; ++i) {
    if (i < 0 || static_cast<std::size_t>(i) >= staff.measures.size()) continue;
    const auto& measure = staff.measures[static_cast<std::size_t>(i)];

    // Independent voices have no single linear reading, so none is invented.
    if (measure.voices.size() > 1) return StaffChange::SequenceState::Unavailable;

    for (const auto& voice : measure.voices) {
      for (const auto& el : voice.elements) {
        if (!el.is_chord()) continue;
        ++chord_id;
        for (const auto& note : el.notes) {
          if (notes.size() >= kMaxSequenceNotes) {
            return StaffChange::SequenceState::Truncated;
          }
          notes.push_back(note);
          ticks.push_back(el.ticks);
          measure_numbers.push_back(measure.number);
          chord_ids.push_back(chord_id);
        }
      }
    }
  }
  return state;
}

/// One alignment step's contribution to a staff's changes.
struct StepResult {
  bool changed = false;
  std::vector<NoteChange> notes;
  int measures_added = 0;
  int measures_removed = 0;
  int display_measure = 0;
};

StepResult evaluate_step(const MeasureLink& link, const Staff& before_staff,
                         const Staff& after_staff) {
  StepResult result;

  const auto* before_measure =
      link.before >= 0 && static_cast<std::size_t>(link.before) < before_staff.measures.size()
          ? &before_staff.measures[static_cast<std::size_t>(link.before)]
          : nullptr;
  const auto* after_measure =
      link.after >= 0 && static_cast<std::size_t>(link.after) < after_staff.measures.size()
          ? &after_staff.measures[static_cast<std::size_t>(link.after)]
          : nullptr;

  if (after_measure != nullptr) {
    result.display_measure = after_measure->number;
  } else if (before_measure != nullptr) {
    result.display_measure = before_measure->number;
  }

  switch (link.op) {
    case MeasureOp::Match:
      if (before_measure == nullptr || after_measure == nullptr) break;
      if (!measures_differ(*before_measure, *after_measure)) break;
      result.changed = true;
      result.notes = diff_measure(*before_measure, *after_measure, result.display_measure);
      break;

    case MeasureOp::Insert:
      if (after_measure == nullptr) break;
      result.changed = true;
      result.measures_added = 1;
      result.notes =
          whole_measure_change(*after_measure, result.display_measure, NoteChange::Kind::Added);
      break;

    case MeasureOp::Delete:
      if (before_measure == nullptr) break;
      result.changed = true;
      result.measures_removed = 1;
      result.notes = whole_measure_change(*before_measure, result.display_measure,
                                          NoteChange::Kind::Removed);
      break;
  }

  return result;
}

void tally(StaffChange& change, const std::vector<NoteChange>& notes) {
  for (const auto& note : notes) {
    switch (note.kind) {
      case NoteChange::Kind::PitchChanged:
      case NoteChange::Kind::DurationChanged:
        ++change.notes_changed;
        break;
      case NoteChange::Kind::Added:
        ++change.notes_added;
        break;
      case NoteChange::Kind::Removed:
        ++change.notes_removed;
        break;
      case NoteChange::Kind::ArticulationChanged:
        ++change.articulations_changed;
        break;
    }
  }
}

/// Walks one staff's alignment, grouping consecutive changed measures into runs.
void diff_staff(const Staff& before_staff, const Staff& after_staff, const score::Part* part,
                const Alignment& alignment, const Options& options,
                std::vector<StaffChange>& out) {
  std::optional<StaffChange> run;
  int run_first_before = -1;
  int run_last_before = -1;
  int run_first_after = -1;
  int run_last_after = -1;

  const auto flush = [&]() {
    if (!run) return;
    if (run->total_changes() > 0) {
      // Sequences are for display and transform detection; failure to collect them (a
      // multi-voice or very long run) simply leaves them empty.
      using State = StaffChange::SequenceState;
      auto state = State::Complete;

      if (run_first_before >= 0) {
        state = collect_sequence(before_staff, run_first_before, run_last_before,
                                 run->before_seq, run->before_ticks, run->before_measures,
                                 run->before_chords);
      }
      if (run_first_after >= 0 && state != State::Unavailable) {
        const auto after_state =
            collect_sequence(after_staff, run_first_after, run_last_after, run->after_seq,
                             run->after_ticks, run->after_measures, run->after_chords);
        // The weaker of the two states describes the pair.
        if (after_state != State::Complete) state = after_state;
      }

      run->sequence_state = state;
      if (state == State::Unavailable) {
        run->before_seq.clear();
        run->before_ticks.clear();
        run->before_measures.clear();
        run->before_chords.clear();
        run->after_seq.clear();
        run->after_ticks.clear();
        run->after_measures.clear();
        run->after_chords.clear();
      }
      out.push_back(std::move(*run));
    }
    run.reset();
    run_first_before = run_last_before = run_first_after = run_last_after = -1;
  };

  for (const auto& link : alignment) {
    const auto step = evaluate_step(link, before_staff, after_staff);

    if (!step.changed) {
      flush();
      continue;
    }

    if (options.measures) {
      const auto [lo, hi] = *options.measures;
      if (step.display_measure < lo || step.display_measure > hi) {
        flush();
        continue;
      }
    }

    if (!run) {
      run.emplace();
      run->staff_id = after_staff.id;
      run->part_index = after_staff.part_index;
      run->part_name = part != nullptr ? part->display_name() : fmt::format("Staff {}", after_staff.id);
      run->staff_label = staff_label(part, after_staff);
      run->first_measure = step.display_measure;
    }

    run->last_measure = step.display_measure;
    run->measures_added += step.measures_added;
    run->measures_removed += step.measures_removed;
    tally(*run, step.notes);
    run->notes.insert(run->notes.end(), step.notes.begin(), step.notes.end());

    if (link.before >= 0) {
      if (run_first_before < 0) run_first_before = link.before;
      run_last_before = link.before;
    }
    if (link.after >= 0) {
      if (run_first_after < 0) run_first_after = link.after;
      run_last_after = link.after;
    }
  }

  flush();
}

/// Records key signature changes, once per measure, listing the staves affected.
void diff_key_signatures(const Score& before, const Score& after,
                         const std::vector<std::pair<int, int>>& staff_pairs,
                         const Alignment& alignment, std::vector<KeySigChange>& out) {
  for (const auto& link : alignment) {
    if (link.op != MeasureOp::Match) continue;

    std::vector<std::string> affected;
    int display_measure = 0;

    for (const auto& [before_id, after_id] : staff_pairs) {
      const auto* bs = before.staff_by_id(before_id);
      const auto* as = after.staff_by_id(after_id);
      if (bs == nullptr || as == nullptr) continue;
      if (static_cast<std::size_t>(link.before) >= bs->measures.size()) continue;
      if (static_cast<std::size_t>(link.after) >= as->measures.size()) continue;

      const auto& bm = bs->measures[static_cast<std::size_t>(link.before)];
      const auto& am = as->measures[static_cast<std::size_t>(link.after)];
      display_measure = am.number;

      // Compare each staff against its own earlier self, so a transposing staff is
      // judged on the signature it actually prints.
      const auto written = [](const std::optional<score::KeySig>& ks) -> std::optional<int> {
        if (!ks) return std::nullopt;
        return ks->actual.value_or(ks->concert);
      };

      const auto b_key = written(bm.key_sig);
      const auto a_key = written(am.key_sig);
      if (b_key == a_key) continue;

      affected.push_back(staff_label(after.part_for_staff(as->id), *as));
    }

    if (affected.empty()) continue;

    KeySigChange change;
    change.measure = display_measure;
    // The reported values are concert pitch, read from non-transposing staves.
    change.before = before.key_signature(display_measure).value_or(0);
    change.after = after.key_signature(display_measure).value_or(0);
    change.all_staves = affected.size() == staff_pairs.size();
    change.staves = std::move(affected);
    out.push_back(std::move(change));
  }
}

/// Counts inserted and deleted measures once for the whole score.
///
/// Measure numbers for the range filter are read from whichever side of the alignment
/// the measure exists on.
void count_measure_changes(const Score& after,
                           const std::vector<std::pair<int, int>>& staff_pairs,
                           const Alignment& alignment, const Options& options, Stats& stats) {
  if (staff_pairs.empty()) return;
  const auto* reference = after.staff_by_id(staff_pairs.front().second);

  int running_number = 1;
  for (const auto& link : alignment) {
    if (link.op == MeasureOp::Match) {
      if (reference != nullptr && static_cast<std::size_t>(link.after) < reference->measures.size()) {
        running_number = reference->measures[static_cast<std::size_t>(link.after)].number;
      }
      continue;
    }

    if (link.op == MeasureOp::Insert && reference != nullptr &&
        static_cast<std::size_t>(link.after) < reference->measures.size()) {
      running_number = reference->measures[static_cast<std::size_t>(link.after)].number;
    }

    if (options.measures) {
      const auto [lo, hi] = *options.measures;
      if (running_number < lo || running_number > hi) continue;
    }

    if (link.op == MeasureOp::Insert) {
      ++stats.measures_added;
    } else {
      ++stats.measures_removed;
    }
  }
}

void diff_time_signatures(const Score& before, const Score& after,
                          const std::vector<std::pair<int, int>>& staff_pairs,
                          const Alignment& alignment, std::vector<TimeSigChange>& out) {
  if (staff_pairs.empty()) return;

  const auto* bs = before.staff_by_id(staff_pairs.front().first);
  const auto* as = after.staff_by_id(staff_pairs.front().second);
  if (bs == nullptr || as == nullptr) return;

  for (const auto& link : alignment) {
    if (link.op != MeasureOp::Match) continue;
    if (static_cast<std::size_t>(link.before) >= bs->measures.size()) continue;
    if (static_cast<std::size_t>(link.after) >= as->measures.size()) continue;

    const auto& bm = bs->measures[static_cast<std::size_t>(link.before)];
    const auto& am = as->measures[static_cast<std::size_t>(link.after)];
    if (!bm.time_sig || !am.time_sig) continue;
    if (bm.time_sig->n == am.time_sig->n && bm.time_sig->d == am.time_sig->d) continue;

    out.push_back(TimeSigChange{.measure = am.number, .before = *bm.time_sig, .after = *am.time_sig});
  }
}

}  // namespace

std::vector<std::pair<int, int>> match_parts(const Score& before, const Score& after,
                                             OrchestrationChange& out) {
  std::vector<int> before_match(before.parts.size(), -1);
  std::vector<int> after_match(after.parts.size(), -1);

  // Strongest correspondences first, so an exact instrument-and-name match is never
  // stolen by a weaker candidate earlier in the score.
  for (int required = 4; required >= 1; --required) {
    for (std::size_t i = 0; i < before.parts.size(); ++i) {
      if (before_match[i] >= 0) continue;
      for (std::size_t j = 0; j < after.parts.size(); ++j) {
        if (after_match[j] >= 0) continue;
        if (part_affinity(before.parts[i], after.parts[j]) != required) continue;
        before_match[i] = static_cast<int>(j);
        after_match[j] = static_cast<int>(i);
        break;
      }
    }
  }

  std::vector<std::pair<int, int>> staff_pairs;

  for (std::size_t i = 0; i < before.parts.size(); ++i) {
    const auto& part = before.parts[i];
    if (before_match[i] < 0) {
      out.changes.push_back(PartChange{.kind = PartChange::Kind::Removed,
                                       .name = part.display_name(),
                                       .old_name = {},
                                       .instrument_id = part.instrument_id});
      continue;
    }

    const auto& counterpart = after.parts[static_cast<std::size_t>(before_match[i])];
    if (part.display_name() != counterpart.display_name()) {
      out.changes.push_back(PartChange{.kind = PartChange::Kind::Renamed,
                                       .name = counterpart.display_name(),
                                       .old_name = part.display_name(),
                                       .instrument_id = counterpart.instrument_id});
    } else {
      out.unchanged.push_back(part.display_name());
    }

    // Staves correspond positionally within a matched part.
    const std::size_t shared = std::min(part.staff_ids.size(), counterpart.staff_ids.size());
    for (std::size_t k = 0; k < shared; ++k) {
      staff_pairs.emplace_back(part.staff_ids[k], counterpart.staff_ids[k]);
    }
  }

  for (std::size_t j = 0; j < after.parts.size(); ++j) {
    if (after_match[j] >= 0) continue;
    const auto& part = after.parts[j];
    out.changes.push_back(PartChange{.kind = PartChange::Kind::Added,
                                     .name = part.display_name(),
                                     .old_name = {},
                                     .instrument_id = part.instrument_id});
  }

  return staff_pairs;
}

ScoreDiff compute(const Score& before, const Score& after, const Options& options) {
  ScoreDiff diff;
  diff.parsed = true;
  diff.title = after.title().empty() ? before.title() : after.title();

  auto staff_pairs = match_parts(before, after, diff.orchestration);
  const auto alignment = align_score(before, after, staff_pairs);

  for (const auto& [before_id, after_id] : staff_pairs) {
    if (options.staff && *options.staff != after_id) continue;

    const auto* before_staff = before.staff_by_id(before_id);
    const auto* after_staff = after.staff_by_id(after_id);
    if (before_staff == nullptr || after_staff == nullptr) continue;

    const auto* part = after.part_for_staff(after_id);
    if (options.instrument) {
      const std::string name = part != nullptr ? part->display_name() : std::string{};
      if (!contains_ignoring_case(name, *options.instrument)) continue;
    }

    diff_staff(*before_staff, *after_staff, part, alignment, options, diff.staves);
  }

  // Orchestration and signature changes are score-wide, so they are suppressed when the
  // caller has narrowed the diff to one instrument.
  if (!options.instrument && !options.staff) {
    diff_key_signatures(before, after, staff_pairs, alignment, diff.key_sigs);
    diff_time_signatures(before, after, staff_pairs, alignment, diff.time_sigs);
  } else {
    diff.orchestration = {};
  }

  // Transformations are read off the completed runs, so detection sees whole phrases
  // rather than individual measures.
  analysis::annotate(diff);

  std::set<int> touched_staves;
  for (const auto& change : diff.staves) {
    diff.stats.notes_changed += change.notes_changed;
    diff.stats.notes_added += change.notes_added;
    diff.stats.notes_removed += change.notes_removed;
    diff.stats.articulations_changed += change.articulations_changed;
    touched_staves.insert(change.staff_id);
  }
  diff.stats.staves_changed = static_cast<int>(touched_staves.size());

  // Measure insertions and deletions are read off the alignment itself, which is
  // score-wide. Summing the per-staff counts would multiply every added bar by the
  // number of instruments playing.
  count_measure_changes(after, staff_pairs, alignment, options, diff.stats);

  for (const auto& change : diff.orchestration.changes) {
    switch (change.kind) {
      case PartChange::Kind::Added:
        ++diff.stats.instruments_added;
        break;
      case PartChange::Kind::Removed:
        ++diff.stats.instruments_removed;
        break;
      case PartChange::Kind::Renamed:
        ++diff.stats.instruments_renamed;
        break;
    }
  }

  return diff;
}

}  // namespace motif::diff
