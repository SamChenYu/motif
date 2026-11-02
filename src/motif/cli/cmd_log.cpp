#include <fmt/format.h>

#include <string>

#include "motif/cli/commands.h"
#include "motif/cli/options.h"
#include "motif/cli/score_source.h"
#include "motif/diff/engine.h"
#include "motif/git/repo.h"
#include "motif/render/log_view.h"

namespace motif::cli {
namespace {

/// How many commits to summarise when no count is given.
constexpr int kDefaultLimit = 15;

void print_help() {
  fmt::print(
      "usage: motif log [-n <count>] [--detail] [options]\n"
      "\n"
      "Summarises each commit by what it did to the music rather than to the file.\n"
      "\n"
      "  motif log              recent commits, one line each\n"
      "  motif log -1           the last commit, in full\n"
      "  motif log -n 5         the last five commits\n"
      "  motif log --detail     every listed commit, in full\n"
      "\n"
      "Options:\n"
      "{}"
      "\n"
      "Use `motif git log` for git's own output.\n",
      common_flag_help());
}

/// Builds the log entry for one commit by diffing it against its first parent.
///
/// Both sides of every changed score come from a single `git diff-tree`, which reports
/// the path and the blob id on each side at once. Starting git processes is the dominant
/// cost of walking a history, so the aim throughout is to issue as few as possible.
render::LogEntry build_entry(const git::Repo& repo, const git::CommitInfo& commit,
                             const diff::Options& diff_options) {
  render::LogEntry entry;
  entry.commit = commit;

  // The first commit has nothing to compare against, so report the score's dimensions.
  if (commit.is_root()) {
    const auto scores = all_tracked_scores(repo);
    if (scores.empty()) {
      entry.note = "no score files changed";
      return entry;
    }

    entry.is_root = true;
    std::string error;
    for (const auto& path : scores) {
      const auto score = load_score(repo, Revision::at(commit.oid), path, error);
      if (!score) continue;
      entry.instrument_count += static_cast<int>(score->parts.size());
      entry.measure_count += score->measure_count();
      entry.note_count += score->note_count();
    }
    return entry;
  }

  std::vector<git::BlobChange> score_changes;
  for (auto& change : repo.changed_blobs(commit.parents.front(), commit.oid)) {
    if (is_score_path(change.path)) score_changes.push_back(std::move(change));
  }

  if (score_changes.empty()) {
    entry.note = "no score files changed";
    return entry;
  }

  // Several scores in one commit are merged into a single change record, so the summary
  // describes the commit rather than one file within it.
  for (const auto& change : score_changes) {
    std::string error;
    const auto before = load_score_by_oid(repo, change.before_oid, error);
    const auto after = load_score_by_oid(repo, change.after_oid, error);
    if (!before || !after) continue;

    auto file_diff = diff::compute(*before, *after, diff_options);
    if (!entry.diff) {
      entry.diff = std::move(file_diff);
      continue;
    }

    entry.diff->staves.insert(entry.diff->staves.end(), file_diff.staves.begin(),
                              file_diff.staves.end());
    entry.diff->key_sigs.insert(entry.diff->key_sigs.end(), file_diff.key_sigs.begin(),
                                file_diff.key_sigs.end());
    entry.diff->orchestration.changes.insert(entry.diff->orchestration.changes.end(),
                                             file_diff.orchestration.changes.begin(),
                                             file_diff.orchestration.changes.end());

    auto& total = entry.diff->stats;
    const auto& add = file_diff.stats;
    total.notes_changed += add.notes_changed;
    total.notes_added += add.notes_added;
    total.notes_removed += add.notes_removed;
    total.articulations_changed += add.articulations_changed;
    total.measures_added += add.measures_added;
    total.measures_removed += add.measures_removed;
    total.instruments_added += add.instruments_added;
    total.instruments_removed += add.instruments_removed;
    total.instruments_renamed += add.instruments_renamed;
  }

  if (!entry.diff) entry.note = "score files could not be read";
  return entry;
}

}  // namespace

int cmd_log(const std::vector<std::string>& args) {
  auto options = parse_options(args, /*accepts_revisions=*/true);
  if (options.help) {
    print_help();
    return 0;
  }
  if (!options.ok()) {
    fmt::print(stderr, "motif log: {}\n", *options.error);
    fmt::print(stderr, "try `motif log --help`, or `motif git log` for git's own output\n");
    return 1;
  }

  const auto repo = git::Repo::discover();
  if (!repo) {
    fmt::print(stderr, "motif: not a git repository\n");
    return 128;
  }

  // `motif log -1` shows one commit in full, matching git's habit of expanding when a
  // single commit is asked for. `--detail` expands however many are listed.
  const bool detail = options.detail || options.limit == 1;
  const int limit = options.limit > 0 ? options.limit : kDefaultLimit;

  const auto commits = repo->log(limit);
  if (commits.empty()) {
    fmt::print("No commits yet.\n");
    return 0;
  }

  const auto diff_options = options.to_diff_options();
  std::vector<render::LogEntry> entries;
  entries.reserve(commits.size());
  for (const auto& commit : commits) {
    entries.push_back(build_entry(*repo, commit, diff_options));
  }

  const auto style = options.style();
  if (detail) {
    render::render_log_detail(entries, style);
  } else {
    render::render_log(entries, style);
  }
  return 0;
}

}  // namespace motif::cli
