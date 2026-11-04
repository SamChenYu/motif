#pragma once

#include <filesystem>
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

/// Runs `argv` to completion on motif's own stdin, stdout and stderr, for programs that
/// need the terminal itself, such as stave's full-screen pager.
///
/// Ctrl+C goes to the child alone while it runs. `out` is always empty; `err` is set only
/// when the program could not be started.
Result run_attached(const std::vector<std::string>& argv);

/// Replaces the current process image with `argv`. Never returns.
///
/// Used for git passthrough so the child inherits our terminal directly: git then sees a
/// tty and keeps its own pager, colouring, progress meters and credential prompts. On
/// Windows, which cannot replace a process, motif runs the child attached and exits
/// with its exit code.
[[noreturn]] void exec(const std::vector<std::string>& argv);

/// The path of the running motif executable, or empty if the OS won't say.
[[nodiscard]] std::filesystem::path self_executable();

}  // namespace motif::proc
