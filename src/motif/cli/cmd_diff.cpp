#include <fmt/format.h>

#include <string>

#include "motif/cli/commands.h"
#include "motif/cli/options.h"
#include "motif/cli/score_source.h"
#include "motif/diff/engine.h"
#include "motif/export/mei.h"
#include "motif/git/repo.h"
#include "motif/render/diff_view.h"
#include "motif/render/pager.h"
#include "motif/render/stave.h"
#include "motif/render/stave_marks.h"

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
      "In a terminal, each score opens engraved in stave as one score that reads\n"
      "straight through: wherever bars changed, they appear as they were, on red with\n"
      "removed and changed notes in red, then as they are, on green with added and\n"
      "changed notes in green. Like git diff, it shows only the changes, with two\n"
      "unchanged bars either side (-U<n> for more or fewer, --full for every bar).\n"
      "n and N jump between changes; q moves on to the next score. --split puts\n"
      "the two versions side by side instead. --raw, --stat, --orchestration and --key\n"
      "print text, as does a diff whose output isn't a terminal.\n"
      "\n"
      "  motif diff                working tree against HEAD\n"
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

/// Where stave is, when this diff should open in it.
std::optional<fs::path> stave_for(const Options& options) {
  if (options.help || !options.ok()) return std::nullopt;
  if (options.raw || options.stat || options.only_orchestration || options.only_key) {
    return std::nullopt;
  }
  // stave needs a terminal to draw in, and keys to read.
  if (!render::stdout_was_tty() || !render::stdin_was_tty()) return std::nullopt;
  return render::find_stave();
}

/// Shows the two versions side by side (--split). Should stave not manage that, shows the
/// newer version alone, marked. Returns false when it couldn't show the score at all.
bool show_split(const fs::path& stave, const diff::ScoreDiff& diff, const score::Score& before,
                const score::Score& after, const std::string& file, const Revision& from,
                const Revision& to, const std::string& summary) {
  const auto comparison = render::stave_comparison(diff, before, after);
  if (comparison.marks == 0) return false;  // nothing to colour
  if (render::show_comparison_in_stave(stave, mei::to_mei(before), mei::to_mei(after), comparison,
                                       fmt::format("{}  {}", file, from.label), to.label,
                                       summary) == 0) {
    return true;
  }
  const auto marks = render::stave_marks(diff, after);
  return marks.marks > 0 &&
         render::show_in_stave(stave, mei::to_mei(after), marks,
                               fmt::format("{}  {} -> {}", file, from.label, to.label),
                               summary) == 0;
}

/// Shows the diff as one score, each changed stretch as it was and then as it is.
/// Returns false when stave couldn't show it.
bool show_unified(const fs::path& stave, const diff::ScoreDiff& diff, const score::Score& before,
                  const score::Score& after, const std::string& file, const Revision& from,
                  const Revision& to, int context, const std::string& summary) {
  const auto unified = render::unified_diff(diff, before, after, from.label, to.label, context);
  if (unified.marks == 0 || unified.mei.empty()) return false;
  render::StaveMarks marks;
  marks.json = unified.json;
  marks.start_bar = unified.start_bar;
  marks.start_staff = unified.start_staff;
  marks.marks = unified.marks;
  return render::show_in_stave(stave, unified.mei, marks,
                               fmt::format("{}  {} -> {}", file, from.label, to.label),
                               summary) == 0;
}

/// Shows one score's diff in stave, with what changed summed up above its status line.
/// Returns false when stave couldn't show it at all, so the text view can be printed
/// instead.
bool show_in_stave(const fs::path& stave, const diff::ScoreDiff& diff, const score::Score& before,
                   const score::Score& after, const fs::path& path, const Revision& from,
                   const Revision& to, bool split, int context) {
  // ASCII only: stave's status line counts bytes, not characters.
  const auto file = path.filename().string();
  std::string summary;
  for (const auto& line : render::summary_lines(diff)) {
    if (!summary.empty()) summary += ", ";
    summary += line;
  }
  return split ? show_split(stave, diff, before, after, file, from, to, summary)
               : show_unified(stave, diff, before, after, file, from, to, context, summary);
}

}  // namespace

bool diff_opens_stave(const std::vector<std::string>& args) {
  return stave_for(parse_options(args, /*accepts_revisions=*/true)).has_value();
}

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

  const auto stave = stave_for(options);
  if (!stave && render::built_with_stave() && !options.raw && !options.stat &&
      !options.only_orchestration && !options.only_key && render::stdout_was_tty() &&
      !render::find_stave()) {
    fmt::print(stderr, "motif: stave isn't next to motif or on PATH, so showing text instead\n");
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

    if (stave && !diff.empty() &&
        show_in_stave(*stave, diff, *before, *after, path, from, to, options.split,
                      options.full ? -1 : options.context.value_or(2))) {
      continue;
    }
    render::render_diff(diff, style, subtitle, view_options(options));
  }

  return exit_code;
}

}  // namespace motif::cli
