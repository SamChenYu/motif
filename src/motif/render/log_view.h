#pragma once

#include <optional>
#include <string>
#include <vector>

#include "motif/diff/change_model.h"
#include "motif/git/repo.h"
#include "motif/render/terminal.h"

namespace motif::render {

/// One commit, with the musical change it introduced.
struct LogEntry {
  git::CommitInfo commit;

  /// The change against the commit's first parent. Absent for a root commit, which has
  /// nothing to compare against.
  std::optional<diff::ScoreDiff> diff;

  bool is_root = false;
  std::string note;  ///< why there is no diff, when there isn't one

  // Dimensions of the score itself, reported for a root commit in place of a diff.
  int instrument_count = 0;
  int measure_count = 0;
  int note_count = 0;
};

/// The compact history: one commit per entry, with a line summarising what changed.
void render_log(const std::vector<LogEntry>& entries, const Style& style);

/// The expanded form: a commit header followed by a per-instrument breakdown.
void render_log_detail(const std::vector<LogEntry>& entries, const Style& style);

}  // namespace motif::render
