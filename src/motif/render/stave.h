#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "motif/render/stave_marks.h"

namespace motif::render {

/// Where stave, the notation pager, is: $MOTIF_STAVE, then next to motif's own
/// executable (where the build puts it), then on PATH. Nothing if it can't be found.
[[nodiscard]] std::optional<std::filesystem::path> find_stave();

/// True when motif was built with stave, so not finding it is worth a mention.
[[nodiscard]] bool built_with_stave();

/// Shows a score full screen in stave, with a diff's marks coloured, until the user
/// quits. `mei` is the score (from mei::to_mei) and `name` is what stave's status line
/// calls it. Returns stave's exit code: non-zero means it couldn't show the score, and
/// has said why on stderr.
///
/// Set MOTIF_DUMP_NOTATION to a folder to keep the files stave was given there.
int show_in_stave(const std::filesystem::path& stave, const std::string& mei,
                  const StaveMarks& marks, const std::string& name);

/// Shows two versions side by side in stave (stave --compare), the older on the left,
/// with each side's marks, until the user quits. Returns stave's exit code, as above.
int show_comparison_in_stave(const std::filesystem::path& stave, const std::string& before_mei,
                             const std::string& after_mei, const StaveComparison& comparison,
                             const std::string& before_name, const std::string& after_name);

}  // namespace motif::render
