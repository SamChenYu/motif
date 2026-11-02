#include "motif/cli/dispatch.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <string_view>

#include "motif/cli/commands.h"
#include "motif/git/subprocess.h"
#include "motif/render/pager.h"

namespace motif::cli {
namespace {

constexpr std::string_view kVersion = "0.1.0";

/// Subcommands motif re-renders. Everything absent from this list goes to git untouched.
constexpr std::array<std::string_view, 4> kMotifCommands{"status", "diff", "log", "inspect"};

void print_usage() {
  fmt::print(
      "motif {} -- a music-native git wrapper\n"
      "\n"
      "usage: motif <command> [<args>]\n"
      "\n"
      "Commands rendered musically:\n"
      "  status         Working-tree changes, by instrument and measure range\n"
      "  diff           Note-level comparison between two revisions\n"
      "  log            Commit history, summarised musically\n"
      "  inspect        Dump the parsed structure of a score file\n"
      "\n"
      "Any other command is passed straight to git, so `motif commit`, `motif push`\n"
      "and friends behave exactly as git does. Use `motif git <args>` to force a\n"
      "command through to git even when motif would otherwise handle it -- for\n"
      "example `motif git diff` for the raw XML diff.\n"
      "\n"
      "Add --no-pager to any command to write straight to stdout. Output is plain\n"
      "text with no colour whenever it is not going to a terminal.\n",
      kVersion);
}

/// Replaces this process with git, forwarding `args` verbatim. Never returns.
[[noreturn]] void forward_to_git(const std::vector<std::string>& args) {
  std::vector<std::string> argv{"git"};
  argv.insert(argv.end(), args.begin(), args.end());
  proc::exec(argv);
}

}  // namespace

bool is_motif_command(std::string_view name) {
  for (const auto& c : kMotifCommands) {
    if (c == name) return true;
  }
  return false;
}

int dispatch(std::vector<std::string> args) {
  if (args.empty()) {
    print_usage();
    return 1;
  }

  const std::string& cmd = args[0];

  if (cmd == "--version" || cmd == "-V") {
    fmt::print("motif {}\n", kVersion);
    return 0;
  }

  // Bare `motif --help` describes motif; `motif help <topic>` is git's business.
  if (args.size() == 1 && (cmd == "--help" || cmd == "-h" || cmd == "help")) {
    print_usage();
    return 0;
  }

  // Explicit escape hatch: `motif git diff` reaches git's own diff.
  if (cmd == "git") {
    forward_to_git({args.begin() + 1, args.end()});
  }

  if (!is_motif_command(cmd)) {
    forward_to_git(args);
  }

  // Capture the terminal's properties before a pager can take stdout away, then page
  // long output the way git does.
  render::init_terminal();

  const bool wants_pager =
      std::find(args.begin(), args.end(), "--no-pager") == args.end() &&
      std::find(args.begin(), args.end(), "--help") == args.end() &&
      std::find(args.begin(), args.end(), "-h") == args.end();

  std::vector<std::string> forwarded;
  for (const auto& arg : args) {
    if (arg != "--no-pager") forwarded.push_back(arg);
  }

  if (wants_pager) render::start_pager();

  int exit_code = 0;
  if (cmd == "status") {
    exit_code = cmd_status(forwarded);
  } else if (cmd == "diff") {
    exit_code = cmd_diff(forwarded);
  } else if (cmd == "log") {
    exit_code = cmd_log(forwarded);
  } else {
    exit_code = cmd_inspect(forwarded);
  }

  render::finish_pager();
  return exit_code;
}

}  // namespace motif::cli
