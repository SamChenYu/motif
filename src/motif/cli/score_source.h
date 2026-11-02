#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "motif/git/repo.h"
#include "motif/score/model.h"

namespace motif::cli {

namespace fs = std::filesystem;

/// Which version of a score to read.
///
/// `rev` holds a git revision; the empty string means the index, and nullopt means the
/// file as it currently sits in the working tree.
struct Revision {
  std::optional<std::string> rev;
  std::string label;

  static Revision working_tree() { return {std::nullopt, "working tree"}; }
  static Revision index() { return {std::string(""), "index"}; }
  static Revision at(std::string name) {
    const std::string label = name;
    return {std::move(name), label};
  }
};

[[nodiscard]] bool is_score_path(const fs::path& path);

/// A parsed score, shared so that callers never copy one.
using ScorePtr = std::shared_ptr<const score::Score>;

/// Reads and parses one version of a score. Returns null when the version does not
/// exist or cannot be parsed, setting `error` in the latter case.
///
/// Results are memoised by git blob id for the life of the process. `motif log` compares
/// each commit with its parent, so every blob is the "after" of one comparison and the
/// "before" of the next; without memoisation each score would be parsed twice, which on
/// a multi-megabyte orchestral score is the dominant cost of the command. Working-tree
/// reads have no blob id and are never cached.
[[nodiscard]] ScorePtr load_score(const git::Repo& repo, const Revision& revision,
                                  const fs::path& rel, std::string& error);

/// Reads and parses a score straight from a blob id, with the same memoisation.
///
/// Lets callers that already know the id -- `log`, which gets both sides from one
/// `git diff-tree` -- skip resolving the path to an object all over again.
[[nodiscard]] ScorePtr load_score_by_oid(const git::Repo& repo, const std::string& oid,
                                         std::string& error);

/// Score files that differ between two revisions, newest state last.
///
/// When `to` is the working tree this consults the working tree rather than a commit,
/// so uncommitted edits are included.
[[nodiscard]] std::vector<fs::path> changed_scores(const git::Repo& repo, const Revision& from,
                                                   const Revision& to);

/// Every score file tracked in the repository, in sorted order.
[[nodiscard]] std::vector<fs::path> all_tracked_scores(const git::Repo& repo);

}  // namespace motif::cli
