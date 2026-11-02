#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "motif/diff/change_model.h"
#include "motif/score/model.h"

namespace motif::diff {

/// Narrows what a diff reports. Filtering happens here rather than at render time so
/// that the summary counts always describe exactly what is shown.
struct Options {
  std::optional<std::string> instrument;         ///< case-insensitive substring match
  std::optional<std::pair<int, int>> measures;   ///< inclusive display-number range
  std::optional<int> staff;                      ///< staff id
};

/// Computes the complete musical difference between two versions of one score.
[[nodiscard]] ScoreDiff compute(const score::Score& before, const score::Score& after,
                                const Options& options = {});

/// Pairs up parts across two versions and records what was added, removed or renamed.
/// Exposed for testing.
[[nodiscard]] std::vector<std::pair<int, int>> match_parts(const score::Score& before,
                                                           const score::Score& after,
                                                           OrchestrationChange& out);

}  // namespace motif::diff
