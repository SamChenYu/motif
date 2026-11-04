#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "motif/diff/engine.h"
#include "motif/render/terminal.h"

namespace motif::cli {

/// Flags shared by motif's own subcommands.
///
/// Parsed by hand rather than with an argument library: motif's commands take git-style
/// positional revisions freely mixed with flags, and everything after `--` is a path.
struct Options {
  bool ascii = false;
  bool no_color = false;
  bool stat = false;
  bool detail = false;  ///< log: expand every listed commit
  bool raw = false;     ///< diff: the text view, never stave's notation
  bool split = false;   ///< diff: the two versions side by side, rather than in one score
  bool help = false;

  std::optional<std::string> instrument;
  std::optional<std::pair<int, int>> measures;
  std::optional<int> staff;

  // Section selectors. When none is given every section is shown.
  bool only_orchestration = false;
  bool only_key = false;

  int limit = -1;  ///< -n <count>, for log

  std::vector<std::string> positional;  ///< revisions
  std::vector<std::string> paths;       ///< everything after `--`

  std::optional<std::string> error;

  [[nodiscard]] bool ok() const { return !error.has_value(); }

  /// Translates the filters into the diff engine's options.
  [[nodiscard]] diff::Options to_diff_options() const {
    diff::Options out;
    out.instrument = instrument;
    out.measures = measures;
    out.staff = staff;
    return out;
  }

  [[nodiscard]] render::Style style() const {
    return render::Style::detect(no_color, ascii);
  }
};

/// Parses `args` (with the subcommand still at index 0).
///
/// `accepts_revisions` controls whether bare words are collected as revisions or
/// rejected as unknown arguments.
[[nodiscard]] Options parse_options(const std::vector<std::string>& args,
                                    bool accepts_revisions);

/// Text describing the flags every command shares, for help output.
[[nodiscard]] const char* common_flag_help();

}  // namespace motif::cli
