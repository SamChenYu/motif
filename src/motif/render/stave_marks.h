#pragma once

#include <optional>
#include <string>

#include "motif/diff/change_model.h"
#include "motif/score/model.h"

namespace motif::render {

/// A diff, as the marks stave colours on the newer score.
struct StaveMarks {
  std::string json;  ///< stave's --marks file: a JSON list of marks

  /// Where the first change is, to open stave there: its bar's printed number, and the
  /// topmost staff changed in that bar (counting from 1). Unset when nothing is marked.
  std::optional<int> start_bar;
  int start_staff = 1;

  int marks = 0;    ///< how many marks the file holds
  int dropped = 0;  ///< changes left out because no note in the newer score matched them
};

/// Builds stave's marks for `diff` on `after`, the score stave will show (written by
/// mei::to_mei, so staff n is after's nth staff and bars keep their measure numbers).
///
/// Added and changed notes are marked one by one; inserted bars and added instruments
/// as whole bars. Removed notes are gone from the newer score, so a bar that lost notes
/// is marked removed, as long as nothing else in that bar of that staff is marked.
/// Bars deleted outright, and removed instruments, can't be shown at all.
///
/// Each note mark is checked against `after`, so that every mark finds its note: stave
/// rejects a marks file in which any mark misses.
[[nodiscard]] StaveMarks stave_marks(const diff::ScoreDiff& diff, const score::Score& after);

}  // namespace motif::render
