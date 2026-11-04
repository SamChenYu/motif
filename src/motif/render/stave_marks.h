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

/// A diff as stave shows it side by side (stave --compare): marks for each version, and
/// which bars of one are which of the other.
struct StaveComparison {
  /// stave's --before-marks, on the older score: removed notes in red, changed notes as
  /// they were in yellow, deleted bars and removed instruments in red.
  std::string before_json;
  /// stave's --marks, on the newer score: added notes in green, changed notes in yellow,
  /// inserted bars and added instruments in green.
  std::string after_json;
  /// stave's --pairs: runs of older bars matched to newer ones, like "1-17=1-17,19-40=18-39".
  std::string pairs;

  /// Where the first change on either side is, as a bar of the newer score (its printed
  /// number) and a staff. Unset when nothing is marked.
  std::optional<int> start_bar;
  int start_staff = 1;

  int marks = 0;    ///< how many marks the two files hold
  int dropped = 0;  ///< changes left out because no note matched them
};

/// Builds the side-by-side view of `diff`, between `before` and `after` as mei::to_mei
/// writes them. As with stave_marks, every note mark is checked against its score.
[[nodiscard]] StaveComparison stave_comparison(const diff::ScoreDiff& diff,
                                               const score::Score& before,
                                               const score::Score& after);

/// A diff as one score that reads straight through, the way a unified diff reads down
/// the page: unchanged bars once, and each changed stretch twice in a row, first as it
/// was, its removed and changed notes in red, then as it is, its added and changed notes
/// in green. Inserted bars are green and deleted ones red. Added instruments are green
/// throughout; removed ones aren't shown.
struct UnifiedDiff {
  std::string mei;   ///< the score, for stave
  std::string json;  ///< stave's --marks for it
  /// Where to open: a bar before the first change (as stave's --bar), and its top staff.
  std::optional<int> start_bar;
  int start_staff = 1;
  int hunks = 0;    ///< how many changed stretches
  int marks = 0;    ///< how many marks the file holds
  int dropped = 0;  ///< changes left out because no note matched them
};

/// Builds the unified view of `diff`. `before_text` and `after_text` label the two
/// halves of each changed stretch, above the top staff ("HEAD", "working tree").
///
/// Like git diff, it shows only the changed stretches and `context` unchanged bars
/// either side of each; where bars are left out, a double barline and a note say how
/// many. A `context` below 0 shows every bar.
[[nodiscard]] UnifiedDiff unified_diff(const diff::ScoreDiff& diff, const score::Score& before,
                                       const score::Score& after, const std::string& before_text,
                                       const std::string& after_text, int context = -1);

}  // namespace motif::render
