#pragma once

#include <string>
#include <vector>

#include "motif/diff/change_model.h"
#include "motif/score/model.h"

namespace motif::diff {

/// Aligns two token sequences, reporting matches, insertions and deletions.
///
/// Common leading and trailing runs are trimmed before the quadratic step, so the cost
/// tracks the size of the *edit* rather than the size of the score -- the usual case is
/// a few changed bars inside a long movement.
[[nodiscard]] Alignment align_tokens(const std::vector<std::string>& before,
                                     const std::vector<std::string>& after);

/// The token used to identify a measure during alignment.
///
/// A measure's `eid` is preferred: it survives edits, so a bar whose notes changed still
/// matches its old self instead of appearing as a delete plus an insert. Only when eids
/// are unavailable does this fall back to hashing the measure's content, which cannot
/// make that distinction.
[[nodiscard]] std::string measure_token(const score::Measure& measure);

/// A content fingerprint of a measure: its notes, rhythms and attributes.
/// Two measures with equal fingerprints are musically identical.
[[nodiscard]] std::string measure_fingerprint(const score::Measure& measure);

/// True when the measure content of two measures differs in any way motif models.
[[nodiscard]] bool measures_differ(const score::Measure& a, const score::Measure& b);

/// Aligns the measures of two scores, once, for the whole score.
///
/// Barlines are score-wide in MuseScore -- every staff holds the same number of measures
/// -- so an inserted bar is a score-wide event and alignment is computed once rather
/// than per staff. `staff_pairs` gives the before/after staff ids that correspond, and
/// is used to find a staff carrying measure eids (in MuseScore 4 only staff 1 does).
[[nodiscard]] Alignment align_score(const score::Score& before, const score::Score& after,
                                    const std::vector<std::pair<int, int>>& staff_pairs);

}  // namespace motif::diff
