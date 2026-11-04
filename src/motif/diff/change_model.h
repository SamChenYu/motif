#pragma once

#include <optional>
#include <string>
#include <vector>

#include "motif/score/model.h"

namespace motif::diff {

// ---------------------------------------------------------------------------
// Tier 1 -- orchestration
// ---------------------------------------------------------------------------

struct PartChange {
  enum class Kind { Added, Removed, Renamed };

  Kind kind = Kind::Added;
  std::string name;      ///< current name (the new one, for a rename)
  std::string old_name;  ///< previous name, for a rename
  std::string instrument_id;
  /// The part's index in the newer score's parts, or the older's for a removal.
  int part_index = -1;
};

struct OrchestrationChange {
  std::vector<PartChange> changes;
  std::vector<std::string> unchanged;  ///< display names, in score order

  [[nodiscard]] bool empty() const { return changes.empty(); }
};

// ---------------------------------------------------------------------------
// Tier 2 -- measure alignment
// ---------------------------------------------------------------------------

enum class MeasureOp { Match, Insert, Delete };

/// One step of a measure alignment. Indices address the respective measure vectors;
/// the unused side is -1.
struct MeasureLink {
  MeasureOp op = MeasureOp::Match;
  int before = -1;
  int after = -1;
};

using Alignment = std::vector<MeasureLink>;

// ---------------------------------------------------------------------------
// Tier 3 -- notes
// ---------------------------------------------------------------------------

struct NoteChange {
  enum class Kind {
    PitchChanged,
    DurationChanged,
    ArticulationChanged,
    Added,
    Removed,
  };

  Kind kind = Kind::PitchChanged;
  int measure = 0;  ///< display number in the newer score
  int voice = 1;
  int onset = 0;         ///< ticks from the barline, in the newer score (the older, for a removal)
  int before_onset = 0;  ///< ticks from the barline, in the older score

  std::optional<score::Pitch> before;
  std::optional<score::Pitch> after;
  int before_ticks = 0;
  int after_ticks = 0;

  /// True when the whole measure was inserted (for Added) or deleted (for Removed). A
  /// deleted measure's number is its number in the older score.
  bool whole_measure = false;
};

/// A detected exact relationship between a run's before and after material.
///
/// Only reported when the relationship holds for every note in the run; motif never
/// reports an approximate or partial transformation.
struct Transform {
  enum class Kind { Transposition, Augmentation, Diminution, Retrograde, Inversion };

  Kind kind = Kind::Transposition;
  int semitones = 0;      ///< for Transposition
  int ratio_num = 1;      ///< for Augmentation / Diminution, as a multiplier
  int ratio_den = 1;
  int note_count = 0;     ///< how many notes the relationship covers
};

/// A contiguous span of changed measures within one staff.
///
/// Runs, not individual measures, are the unit the renderer prints and the unit
/// transform detection examines -- a transposed phrase spans several bars.
struct StaffChange {
  int staff_id = 0;         ///< in the newer score
  int before_staff_id = 0;  ///< the same staff in the older score
  int part_index = -1;
  std::string part_name;
  std::string staff_label;  ///< part name, disambiguated for multi-staff parts

  int first_measure = 0;  ///< display numbers, from the newer score where possible
  int last_measure = 0;

  int notes_changed = 0;
  int notes_added = 0;
  int notes_removed = 0;
  int articulations_changed = 0;
  int measures_added = 0;
  int measures_removed = 0;

  std::vector<NoteChange> notes;
  std::vector<Transform> transforms;

  /// Whether a run's material can be shown as a linear before/after reading.
  enum class SequenceState {
    Complete,     ///< the whole run is present
    Truncated,    ///< long run: the opening is present, the rest elided
    Unavailable,  ///< independent voices, which have no single linear reading
  };

  /// The run's material, in playing order, for before/after display.
  ///
  /// The vectors are parallel: one entry per note. `*_measures` records which bar each
  /// note falls in, so the renderer can draw barlines, and `*_chords` groups notes that
  /// sound together -- without it a violin's double-stop would read as two melody notes.
  /// When the state is Truncated the renderer must say so rather than quietly printing
  /// a prefix.
  std::vector<score::Note> before_seq;
  std::vector<score::Note> after_seq;
  std::vector<int> before_ticks;
  std::vector<int> after_ticks;
  std::vector<int> before_measures;
  std::vector<int> after_measures;
  std::vector<int> before_chords;
  std::vector<int> after_chords;
  SequenceState sequence_state = SequenceState::Unavailable;

  [[nodiscard]] bool has_sequences() const {
    return sequence_state != SequenceState::Unavailable &&
           (!before_seq.empty() || !after_seq.empty());
  }

  [[nodiscard]] int total_changes() const {
    return notes_changed + notes_added + notes_removed + articulations_changed +
           measures_added + measures_removed;
  }
};

struct KeySigChange {
  int measure = 0;
  int before = 0;  ///< accidental count
  int after = 0;
  std::vector<std::string> staves;  ///< labels of the staves affected
  bool all_staves = false;          ///< true when the whole score changed together
};

struct TimeSigChange {
  int measure = 0;
  score::TimeSig before;
  score::TimeSig after;
};

struct Stats {
  int notes_changed = 0;
  int notes_added = 0;
  int notes_removed = 0;
  int articulations_changed = 0;

  /// Score-wide measure counts. Barlines span every staff, so inserting four bars is
  /// one four-measure event -- not four per instrument.
  int measures_added = 0;
  int measures_removed = 0;

  int instruments_added = 0;
  int instruments_removed = 0;
  int instruments_renamed = 0;
  int staves_changed = 0;

  [[nodiscard]] int instruments_changed() const {
    return instruments_added + instruments_removed + instruments_renamed;
  }
};

/// The complete musical difference between two versions of a score.
///
/// `status`, `diff` and `log` are three verbosity levels over this one value; none of
/// them recompute anything.
struct ScoreDiff {
  std::string title;
  bool parsed = false;  ///< false when a side could not be read

  OrchestrationChange orchestration;
  std::vector<StaffChange> staves;
  std::vector<KeySigChange> key_sigs;
  std::vector<TimeSigChange> time_sigs;
  Stats stats;

  /// Measures inserted in the newer score, as indices into its staves' measures. Every
  /// staff gains the bar, so this is score-wide, like the barlines.
  std::vector<int> inserted_measures;

  /// Which measure of the older score is which of the newer, for the whole score: the
  /// same alignment every staff was compared by.
  Alignment alignment;

  [[nodiscard]] bool empty() const {
    return orchestration.empty() && staves.empty() && key_sigs.empty() && time_sigs.empty();
  }
};

}  // namespace motif::diff
