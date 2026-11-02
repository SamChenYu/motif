#pragma once

#include <vector>

#include "motif/diff/change_model.h"
#include "motif/score/model.h"

namespace motif::diff {

/// Compares two versions of the same measure, note by note.
///
/// Elements are matched by `eid` first -- MuseScore assigns every chord, rest and note a
/// persistent identity, so an edited chord is recognised as the same chord rather than
/// as one deleted and one added. Elements without an eid fall back to matching on their
/// position in the bar.
///
/// `measure_number` is recorded on each change for display.
[[nodiscard]] std::vector<NoteChange> diff_measure(const score::Measure& before,
                                                   const score::Measure& after,
                                                   int measure_number);

/// Every note of a measure, in playing order, treating it as wholly added or removed.
/// Used for measures that exist on only one side of the alignment.
[[nodiscard]] std::vector<NoteChange> whole_measure_change(const score::Measure& measure,
                                                           int measure_number,
                                                           NoteChange::Kind kind);

}  // namespace motif::diff
