#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "motif/git/subprocess.h"

namespace motif::git {

namespace fs = std::filesystem;

/// One commit, as reported by `git log`.
struct CommitInfo {
  std::string oid;      ///< full 40-char sha
  std::string author;
  std::string date;     ///< ISO 8601, author date
  std::string subject;

  /// Parent object ids, empty for a root commit.
  ///
  /// Read from the same `git log` invocation that produced everything else: resolving
  /// parents separately would double the number of git processes motif starts, and
  /// process startup dominates the cost of walking a history.
  std::vector<std::string> parents;

  [[nodiscard]] std::string short_oid(std::size_t n = 6) const {
    return oid.substr(0, std::min(n, oid.size()));
  }

  [[nodiscard]] bool is_root() const { return parents.empty(); }
};

/// One path changed between two trees, with the blob on each side.
struct BlobChange {
  fs::path path;
  std::string before_oid;  ///< all zeroes when the path was added
  std::string after_oid;   ///< all zeroes when the path was deleted
  char status = 'M';       ///< A added, M modified, D deleted, R renamed, ...

  [[nodiscard]] bool added() const { return status == 'A'; }
  [[nodiscard]] bool deleted() const { return status == 'D'; }
};

/// Working-tree state for one path, from `git status --porcelain=v2`.
struct StatusEntry {
  /// Two-character staged/unstaged code. Porcelain v2 uses '.' for "no change", so a
  /// file edited but not staged is ".M" and one staged for addition is "A.".
  std::string xy;
  fs::path path;
  std::optional<fs::path> orig_path;  ///< source path, for renames and copies
  bool untracked = false;

  [[nodiscard]] bool staged() const { return !untracked && !xy.empty() && xy[0] != '.'; }
  [[nodiscard]] bool unstaged() const { return !untracked && xy.size() > 1 && xy[1] != '.'; }
};

/// Branch position, from the `# branch.*` headers of `git status --branch`.
struct BranchInfo {
  std::string head;                       ///< branch name, or "(detached)"
  std::optional<std::string> upstream;
  int ahead = 0;
  int behind = 0;
};

struct Status {
  BranchInfo branch;
  std::vector<StatusEntry> entries;
};

/// A git repository, driven by invoking the `git` binary.
///
/// Every call goes through `git -C <root>`, so the process cwd is irrelevant and no
/// chdir is ever needed. Commands are built as argv arrays and never as shell strings.
class Repo {
 public:
  /// Locates the repository containing the current working directory.
  /// Returns nullopt when cwd is not inside a git repository.
  static std::optional<Repo> discover();

  [[nodiscard]] const fs::path& root() const noexcept { return root_; }

  /// Resolves a revision (`HEAD`, `main`, `8f92a1`, ...) to a full object id.
  [[nodiscard]] std::optional<std::string> rev_parse(std::string_view rev) const;

  /// Object id of `rel` as it exists at `rev`, or nullopt when the path does not exist
  /// there -- which is how an instrument file added or deleted in a commit is detected.
  [[nodiscard]] std::optional<std::string> blob_oid_at(std::string_view rev,
                                                       const fs::path& rel) const;

  /// Raw bytes of a blob. Binary-safe: .mscz blobs are zip archives.
  [[nodiscard]] std::optional<std::string> read_blob(std::string_view oid) const;

  /// Convenience: the content of `rel` at `rev`, or nullopt if absent.
  [[nodiscard]] std::optional<std::string> read_file_at(std::string_view rev,
                                                        const fs::path& rel) const;

  /// Most recent `max` commits, newest first. Restricted to `rel` when it is non-empty.
  [[nodiscard]] std::vector<CommitInfo> log(int max, const fs::path& rel = {}) const;

  [[nodiscard]] Status status() const;

  /// Paths changed between two revisions. `to` may be empty to mean the working tree.
  [[nodiscard]] std::vector<fs::path> changed_paths(std::string_view from,
                                                    std::string_view to) const;

  /// Paths changed between two commits, together with the blob id on each side.
  ///
  /// A single `git diff-tree` answers what `git diff --name-only` plus two
  /// `git rev-parse <rev>:<path>` calls would, per path. Since starting git processes is
  /// the dominant cost of walking a history, collapsing them matters more than any
  /// caching further up.
  [[nodiscard]] std::vector<BlobChange> changed_blobs(std::string_view from,
                                                      std::string_view to) const;

  /// Runs a git subcommand under this repo, capturing output.
  [[nodiscard]] proc::Result git(std::vector<std::string> args) const;

 private:
  explicit Repo(fs::path root) : root_(std::move(root)) {}
  fs::path root_;
};

}  // namespace motif::git
