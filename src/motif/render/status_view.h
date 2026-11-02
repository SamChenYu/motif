#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "motif/diff/change_model.h"
#include "motif/git/repo.h"
#include "motif/render/terminal.h"

namespace motif::render {

/// One score file's change, ready to print.
struct ScoreFileChange {
  std::filesystem::path path;
  std::optional<diff::ScoreDiff> diff;
  std::string note;  ///< why there is no diff: "added", "deleted", or an error
};

/// Everything `motif status` needs to print, gathered before any output happens.
struct StatusReport {
  git::BranchInfo branch;
  std::vector<ScoreFileChange> staged;
  std::vector<ScoreFileChange> unstaged;
  std::vector<ScoreFileChange> untracked;
  std::vector<std::string> other_paths;  ///< changed files that are not scores
};

void render_status(const StatusReport& report, const Style& style);

}  // namespace motif::render
