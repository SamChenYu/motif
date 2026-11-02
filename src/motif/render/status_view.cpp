#include "motif/render/status_view.h"

#include <fmt/format.h>

#include <algorithm>

#include "motif/score/pitch.h"

namespace motif::render {
namespace {

/// Column width for the MODIFIED / ADDED / REMOVED marker.
constexpr std::size_t kLabelWidth = 11;

/// One printable line of the changed-region list.
struct Line {
  std::string label;
  std::string subject;
  std::string detail;
};

std::vector<Line> lines_for(const diff::ScoreDiff& diff, const Style& style) {
  std::vector<Line> lines;

  for (const auto& change : diff.orchestration.changes) {
    switch (change.kind) {
      case diff::PartChange::Kind::Added:
        lines.push_back({"ADDED", change.name, {}});
        break;
      case diff::PartChange::Kind::Removed:
        lines.push_back({"REMOVED", change.name, {}});
        break;
      case diff::PartChange::Kind::Renamed:
        lines.push_back({"RENAMED", change.name, fmt::format("was {}", change.old_name)});
        break;
    }
  }

  for (const auto& change : diff.staves) {
    lines.push_back({"MODIFIED", change.staff_label,
                     measure_range(style, change.first_measure, change.last_measure)});
  }
  for (const auto& change : diff.key_sigs) {
    lines.push_back({"MODIFIED", "Key signature", fmt::format("m. {}", change.measure)});
  }
  for (const auto& change : diff.time_sigs) {
    lines.push_back({"MODIFIED", "Time signature", fmt::format("m. {}", change.measure)});
  }

  return lines;
}

std::string colour_label(const Style& style, const std::string& label) {
  if (label == "ADDED") return green(style, label);
  if (label == "REMOVED") return red(style, label);
  if (label == "RENAMED") return cyan(style, label);
  return yellow(style, label);
}

void print_file(const ScoreFileChange& file, const Style& style, bool show_path) {
  if (show_path) {
    fmt::print("  {}\n", bold(style, file.path.filename().string()));
  }

  if (!file.note.empty()) {
    fmt::print("    {}\n", dim(style, file.note));
    return;
  }
  if (!file.diff) return;

  const auto lines = lines_for(*file.diff, style);
  if (lines.empty()) {
    fmt::print("    {}\n", dim(style, "no musical change (file differs but the notes do not)"));
    return;
  }

  std::size_t subject_width = 0;
  for (const auto& line : lines) {
    subject_width = std::max(subject_width, display_width(line.subject));
  }

  for (const auto& line : lines) {
    fmt::print("  {}{}", pad_right("", 2), colour_label(style, line.label));
    // Pad after colouring, so escape sequences do not count toward the column.
    fmt::print("{}", std::string(kLabelWidth - std::min(kLabelWidth, display_width(line.label)),
                                 ' '));
    if (line.detail.empty()) {
      fmt::print("{}\n", line.subject);
    } else {
      fmt::print("{}   {}\n", pad_right(line.subject, subject_width), dim(style, line.detail));
    }
  }
}

/// The aggregate counts, printed only for what actually changed.
void print_summary(const std::vector<const diff::ScoreDiff*>& diffs, const Style& style) {
  diff::Stats total;
  std::vector<std::string> key_lines;

  for (const auto* d : diffs) {
    total.notes_changed += d->stats.notes_changed;
    total.notes_added += d->stats.notes_added;
    total.notes_removed += d->stats.notes_removed;
    total.articulations_changed += d->stats.articulations_changed;
    total.measures_added += d->stats.measures_added;
    total.measures_removed += d->stats.measures_removed;
    total.instruments_added += d->stats.instruments_added;
    total.instruments_removed += d->stats.instruments_removed;
    total.instruments_renamed += d->stats.instruments_renamed;

    for (const auto& change : d->key_sigs) {
      key_lines.push_back(fmt::format(
          "{} {} {} at m. {}", score::key_signature_name(change.before),
          glyph_arrow_change(style), score::key_signature_name(change.after), change.measure));
    }
  }

  const bool anything = total.notes_changed || total.notes_added || total.notes_removed ||
                        total.articulations_changed || total.measures_added ||
                        total.measures_removed || total.instruments_changed() ||
                        !key_lines.empty();
  if (!anything) return;

  fmt::print("\n{}\n", bold(style, "Musical summary:"));

  const auto plural = [](int n, const char* singular, const char* plural_form) {
    return n == 1 ? singular : plural_form;
  };

  if (total.notes_changed > 0) {
    fmt::print("  ~ {} {} changed\n", total.notes_changed,
               plural(total.notes_changed, "note", "notes"));
  }
  if (total.notes_added > 0) {
    fmt::print("  + {} {} added\n", total.notes_added, plural(total.notes_added, "note", "notes"));
  }
  if (total.notes_removed > 0) {
    fmt::print("  - {} {} removed\n", total.notes_removed,
               plural(total.notes_removed, "note", "notes"));
  }
  if (total.articulations_changed > 0) {
    fmt::print("  ~ {} {} changed\n", total.articulations_changed,
               plural(total.articulations_changed, "articulation", "articulations"));
  }
  if (total.measures_added > 0) {
    fmt::print("  + {} {}\n", total.measures_added,
               plural(total.measures_added, "measure", "measures"));
  }
  if (total.measures_removed > 0) {
    fmt::print("  - {} {}\n", total.measures_removed,
               plural(total.measures_removed, "measure", "measures"));
  }
  if (total.instruments_added > 0) {
    fmt::print("  + {} {}\n", total.instruments_added,
               plural(total.instruments_added, "instrument", "instruments"));
  }
  if (total.instruments_removed > 0) {
    fmt::print("  - {} {}\n", total.instruments_removed,
               plural(total.instruments_removed, "instrument", "instruments"));
  }
  if (total.instruments_renamed > 0) {
    fmt::print("  ~ {} {} renamed\n", total.instruments_renamed,
               plural(total.instruments_renamed, "instrument", "instruments"));
  }
  for (const auto& line : key_lines) {
    fmt::print("  ~ key signature {}\n", line);
  }
}

void print_section(const char* heading, const std::vector<ScoreFileChange>& files,
                   const Style& style, const char* hint) {
  if (files.empty()) return;

  fmt::print("\n{}\n", bold(style, heading));
  if (hint != nullptr) fmt::print("  {}\n", dim(style, hint));
  fmt::print("\n");

  const bool show_paths = files.size() > 1;
  for (const auto& file : files) {
    print_file(file, style, show_paths);
  }
}

}  // namespace

void render_status(const StatusReport& report, const Style& style) {
  fmt::print("On branch {}\n", bold(style, report.branch.head));

  if (report.branch.upstream) {
    const int ahead = report.branch.ahead;
    const int behind = report.branch.behind;
    if (ahead > 0 && behind > 0) {
      fmt::print("Your branch and '{}' have diverged by {} and {} commits.\n",
                 *report.branch.upstream, ahead, behind);
    } else if (ahead > 0) {
      fmt::print("Your branch is {} commit{} ahead of '{}'.\n", ahead, ahead == 1 ? "" : "s",
                 *report.branch.upstream);
    } else if (behind > 0) {
      fmt::print("Your branch is {} commit{} behind '{}'.\n", behind, behind == 1 ? "" : "s",
                 *report.branch.upstream);
    }
  }

  print_section("Changes to be committed:", report.staged, style, nullptr);
  print_section("Changes not staged for commit:", report.unstaged, style, nullptr);
  print_section("Untracked scores:", report.untracked, style, nullptr);

  std::vector<const diff::ScoreDiff*> diffs;
  for (const auto* group : {&report.staged, &report.unstaged}) {
    for (const auto& file : *group) {
      if (file.diff) diffs.push_back(&*file.diff);
    }
  }
  print_summary(diffs, style);

  if (!report.other_paths.empty()) {
    fmt::print("\n{}\n", bold(style, "Other changed files:"));
    for (const auto& path : report.other_paths) {
      fmt::print("  {}\n", path);
    }
  }

  if (report.staged.empty() && report.unstaged.empty() && report.untracked.empty() &&
      report.other_paths.empty()) {
    fmt::print("\nNothing to commit, working tree clean.\n");
  }
}

}  // namespace motif::render
