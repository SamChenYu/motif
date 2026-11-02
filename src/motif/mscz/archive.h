#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motif::mscz {

/// Extracts the MuseScore XML document from a score file's bytes.
///
/// Accepts both container forms: a compressed `.mscz` (a zip archive, whose main
/// document is named by META-INF/container.xml) and a bare uncompressed `.mscx`.
/// Everything else in the archive -- style sheet, audio settings, thumbnail -- is
/// deliberately ignored; none of it affects the notes.
///
/// On failure returns nullopt and, when `error` is non-null, sets it to a description.
[[nodiscard]] std::optional<std::string> extract_mscx(std::string_view bytes,
                                                      std::string* error = nullptr);

/// Reads a score file from disk and extracts its XML document.
[[nodiscard]] std::optional<std::string> extract_mscx_from_file(
    const std::filesystem::path& path, std::string* error = nullptr);

/// Lists the entry names inside a zip archive. Diagnostic helper for `motif inspect`.
[[nodiscard]] std::vector<std::string> zip_entries(std::string_view bytes);

/// True when `bytes` starts with the zip local-file-header magic "PK\3\4".
[[nodiscard]] bool looks_like_zip(std::string_view bytes);

}  // namespace motif::mscz
