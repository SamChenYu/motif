#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "motif/score/model.h"

namespace motif::mscz {

/// Parses a MuseScore XML document into the score IR.
///
/// Unknown elements are skipped rather than rejected: MuseScore's format grows between
/// versions, and a score that uses a feature motif does not model yet should still diff
/// on everything it does model.
[[nodiscard]] std::optional<score::Score> parse_mscx(std::string_view xml,
                                                     std::string* error = nullptr);

/// Extracts and parses a score from raw `.mscz` or `.mscx` bytes.
[[nodiscard]] std::optional<score::Score> parse_score(std::string_view bytes,
                                                      std::string* error = nullptr);

/// Reads, extracts and parses a score file from disk.
[[nodiscard]] std::optional<score::Score> parse_score_file(const std::filesystem::path& path,
                                                           std::string* error = nullptr);

}  // namespace motif::mscz
