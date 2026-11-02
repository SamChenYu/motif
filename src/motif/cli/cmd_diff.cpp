#include <fmt/format.h>

#include <string>

#include "motif/cli/commands.h"
#include "motif/cli/options.h"
#include "motif/cli/score_source.h"
#include "motif/diff/engine.h"
#include "motif/git/repo.h"
#include "motif/render/diff_view.h"

namespace motif::cli {
namespace {

void print_help() {
  fmt::print(
      "usage: motif diff [<rev>] [<rev>] [options] [-- <path>...]\n"
      "\n"
      "Compares two versions of a score musically: which notes changed in which\n"
      "instrument over which measures, which instruments came and went, and how the\n"
      "key signature moved.\n"
      "\n"
      "  motif diff                  working tree against HEAD\n"
      "  motif diff <rev>            working tree against <rev>\n"
      "  motif diff <rev> <rev>      one revision against another\n"
      "\n"
      "Options:\n"
      "{}"
      "\n"
      "Use `motif git diff` for git's own textual diff.\n",
      common_flag_help());
}

/// Works out which two versions to compare from the positional revisions given.
std::pair<Revision, Revision> resolve_revisions(const Options& options) {
  switch (options.positional.size()) {
    case 0:
      return {Revision::at("HEAD"), Revision::working_tree()};
    case 1:
      return {Revision::at(options.positional[0]), Revision::working_tree()};
    default:
      return {Revision::at(options.positional[0]), Revision::at(options.positional[1])};
  }
}

render::DiffViewOptions view_options(const Options& options) {
  render::DiffViewOptions view;
  view.stat_only = options.stat;

  // A section selector narrows the output; giving none shows everything.
  const bool selective = options.only_orchestration || options.only_key;
  if (selective) {
    view.show_staves = false;
    view.show_orchestration = options.only_orchestration;
    view.show_key = options.only_key;
  }
  return view;
}

}  // namespace

int cmd_diff(const std::vector<std::string>& args) {
  auto options = parse_options(args, /*accepts_revisions=*/true);
  if (options.help) {
    print_help();
    return 0;
  }
  if (!options.ok()) {
    fmt::print(stderr, "motif diff: {}\n", *options.error);
    fmt::print(stderr, "try `motif diff --help`, or `motif git diff` for git's own diff\n");
    return 1;
  }

  const auto repo = git::Repo::discover();
  if (!repo) {
    fmt::print(stderr, "motif: not a git repository\n");
    return 128;
  }

  const auto [from, to] = resolve_revisions(options);

  for (const auto& rev : {from, to}) {
    if (rev.rev && !rev.rev->empty() && !repo->rev_parse(*rev.rev)) {
      fmt::print(stderr, "motif: unknown revision '{}'\n", *rev.rev);
      return 128;
    }
  }

  // An explicit path list wins; otherwise diff whatever scores actually changed.
  std::vector<fs::path> scores;
  if (!options.paths.empty()) {
    for (const auto& path : options.paths) scores.emplace_back(path);
  } else {
    scores = changed_scores(*repo, from, to);
  }

  if (scores.empty()) {
    fmt::print("No score files changed between {} and {}.\n", from.label, to.label);
    return 0;
  }

  const auto style = options.style();
  const auto subtitle = fmt::format("{} {} {}", from.label, style.unicode ? "→" : "->", to.label);
  const auto diff_options = options.to_diff_options();

  int exit_code = 0;
  for (const auto& path : scores) {
    std::string before_error;
    std::string after_error;
    const auto before = load_score(*repo, from, path, before_error);
    const auto after = load_score(*repo, to, path, after_error);

    if (scores.size() > 1) {
      fmt::print("\n{}\n", path.generic_string());
    }

    if (!before && !after) {
      fmt::print(stderr, "motif: cannot read '{}' at either revision\n", path.generic_string());
      exit_code = 1;
      continue;
    }
    if (!before) {
      fmt::print("{} is new in {}{}\n", path.generic_string(), to.label,
                 before_error.empty() ? "" : fmt::format(" ({})", before_error));
      continue;
    }
    if (!after) {
      fmt::print("{} was deleted in {}{}\n", path.generic_string(), to.label,
                 after_error.empty() ? "" : fmt::format(" ({})", after_error));
      continue;
    }

    auto diff = diff::compute(*before, *after, diff_options);
    // Many scores leave workTitle empty (MIDI imports always do), in which case the
    // filename is the only name the piece has.
    if (diff.title.empty()) diff.title = path.stem().string();

    render::render_diff(diff, style, subtitle, view_options(options));
  }

  return exit_code;
}

}  // namespace motif::cli
