#pragma once

#include <vector>

#include "motif/diff/change_model.h"
#include "motif/score/model.h"

namespace motif::analysis {

/// Minimum notes before a relationship is worth reporting.
///
/// Two notes a step apart are a coincidence, not a transposition; three is the shortest
/// span where an exact match across every note says something real.
inline constexpr std::size_t kMinTransformNotes = 3;

/// Detects exact relationships between a run's material before and after an edit.
///
/// Every test here is an equality, not a similarity: a transposition is reported only
/// when *every* note moved by the same interval and *every* duration was preserved. A
/// run where one note differs from the pattern yields nothing at all. This is deliberate
/// -- a diff that guesses at musical intent and gets it wrong is worse than one that
/// simply shows the notes.
[[nodiscard]] std::vector<diff::Transform> detect(const std::vector<score::Note>& before,
                                                  const std::vector<int>& before_ticks,
                                                  const std::vector<score::Note>& after,
                                                  const std::vector<int>& after_ticks);

/// Annotates each run of a diff with the transformations it exhibits.
void annotate(diff::ScoreDiff& diff);

}  // namespace motif::analysis
