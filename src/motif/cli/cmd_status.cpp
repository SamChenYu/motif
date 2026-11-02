#include <fmt/format.h>

#include <optional>
#include <string>

#include "motif/cli/commands.h"
#include "motif/cli/options.h"
#include "motif/cli/score_source.h"
#include "motif/diff/engine.h"
#include "motif/git/repo.h"
#include "motif/render/status_view.h"
#include "motif/render/terminal.h"

namespace motif::cli {
namespace {

void print_help() {
  fmt::print(
      "usage: motif status [--ascii] [--no-color]\n"
      "\n"
      "Shows working-tree changes to MuseScore files as musical changes: which\n"
      "instruments were touched, over which measures, and what changed in them.\n"
      "\n"
      "Files that are not scores are listed by name only. Use `motif git status`\n"
      "for git's own output.\n");
}

/// Builds the change record for one score file between two versions.
render::ScoreFileChange compare(const git::Repo& repo, const fs::path& rel,
                                const Revision& before_rev, const Revision& after_rev) {
  render::ScoreFileChange result;
  result.path = rel;

  std::string before_error;
  std::string after_error;
  const auto before = load_score(repo, before_rev, rel, before_error);
  const auto after = load_score(repo, after_rev, rel, after_error);

  if (!before && !after) {
    result.note = "could not read either version";
    return result;
  }
  if (!before) {
    result.note = before_error.empty() ? "new score" : fmt::format("new score ({})", before_error);
    return result;
  }
  if (!after) {
    result.note = after_error.empty() ? "score deleted" : fmt::format("unreadable ({})", after_error);
    return result;
  }

  result.diff = diff::compute(*before, *after);
  return result;
}

}  // namespace

int cmd_status(const std::vector<std::string>& args) {
  const auto options = parse_options(args, /*accepts_revisions=*/false);
  if (options.help) {
    print_help();
    return 0;
  }
  if (!options.ok()) {
    fmt::print(stderr, "motif status: {}\n", *options.error);
    fmt::print(stderr, "try `motif git status` for git's own output\n");
    return 1;
  }

  const auto repo = git::Repo::discover();
  if (!repo) {
    fmt::print(stderr, "motif: not a git repository\n");
    return 128;
  }

  const auto status = repo->status();

  render::StatusReport report;
  report.branch = status.branch;

  for (const auto& entry : status.entries) {
    if (!is_score_path(entry.path)) {
      report.other_paths.push_back(entry.path.generic_string());
      continue;
    }

    if (entry.untracked) {
      render::ScoreFileChange file;
      file.path = entry.path;
      file.note = "untracked -- not yet under version control";
      report.untracked.push_back(std::move(file));
      continue;
    }

    // Staged and unstaged changes are separate comparisons, matching git's own model:
    // HEAD against the index, then the index against the working tree.
    if (entry.staged()) {
      report.staged.push_back(compare(*repo, entry.path, Revision::at("HEAD"), Revision::index()));
    }
    if (entry.unstaged()) {
      report.unstaged.push_back(
          compare(*repo, entry.path, Revision::index(), Revision::working_tree()));
    }
  }

  render::render_status(report, options.style());
  return 0;
}

}  // namespace motif::cli
