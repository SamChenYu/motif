#pragma once

#include <string>
#include <vector>

namespace motif::proc {

/// The captured result of a finished child process.
///
/// `out` and `err` hold raw bytes -- `git cat-file blob` returns binary data for a
/// .mscz, so nothing here may assume text.
struct Result {
  int exit_code = -1;
  std::string out;
  std::string err;

  [[nodiscard]] bool ok() const noexcept { return exit_code == 0; }
};

/// Runs `argv` to completion, capturing stdout and stderr.
///
/// The program is looked up on PATH. No shell is involved, so arguments containing
/// spaces, quotes or globs are passed through verbatim -- which matters, because score
/// filenames routinely contain spaces.
Result run(const std::vector<std::string>& argv);

/// Replaces the current process image with `argv`. Never returns.
///
/// Used for git passthrough so the child inherits our terminal directly: git then sees a
/// tty and keeps its own pager, colouring, progress meters and credential prompts.
[[noreturn]] void exec(const std::vector<std::string>& argv);

}  // namespace motif::proc
