#include "motif/render/log_view.h"

#include <fmt/format.h>

#include <algorithm>
#include <limits>

#include "motif/score/pitch.h"

namespace motif::render {
namespace {

/// The author date, trimmed from ISO 8601 to just the day.
std::string short_date(const std::string& iso) {
  return iso.size() >= 10 ? iso.substr(0, 10) : iso;
}

/// The measure span a diff touches, across every instrument.
std::optional<std::pair<int, int>> touched_range(const diff::ScoreDiff& diff) {
  int first = std::numeric_limits<int>::max();
  int last = std::numeric_limits<int>::min();

  for (const auto& change : diff.staves) {
    first = std::min(first, change.first_measure);
    last = std::max(last, change.last_measure);
  }
  for (const auto& change : diff.key_sigs) {
    first = std::min(first, change.measure);
    last = std::max(last, change.measure);
  }

  if (first > last) return std::nullopt;
  return std::make_pair(first, last);
}

/// The facts worth putting on a one-line summary, most significant first.
std::vector<std::string> summary_parts(const LogEntry& entry, const Style& style) {
  std::vector<std::string> parts;

  if (entry.is_root) {
    parts.push_back(fmt::format("{} instrument{}", entry.instrument_count,
                                entry.instrument_count == 1 ? "" : "s"));
    parts.push_back(fmt::format("{} measures", entry.measure_count));
    parts.push_back(fmt::format("{} notes", entry.note_count));
    return parts;
  }

  if (!entry.diff) {
    if (!entry.note.empty()) parts.push_back(entry.note);
    return parts;
  }

  const auto& diff = *entry.diff;
  if (const auto range = touched_range(diff)) {
    parts.push_back(measure_range(style, range->first, range->second));
  }

  const auto& stats = diff.stats;
  if (stats.measures_added > 0) {
    parts.push_back(fmt::format("+{} measure{}", stats.measures_added,
                                stats.measures_added == 1 ? "" : "s"));
  }
  if (stats.measures_removed > 0) {
    parts.push_back(fmt::format("-{} measure{}", stats.measures_removed,
                                stats.measures_removed == 1 ? "" : "s"));
  }
  if (stats.instruments_changed() > 0) {
    parts.push_back("orchestration");
  }
  if (!diff.key_sigs.empty()) {
    parts.push_back("key signature");
  }
  if (stats.notes_changed > 0) {
    parts.push_back(fmt::format("{} note{} changed", stats.notes_changed,
                                stats.notes_changed == 1 ? "" : "s"));
  }

  int transforms = 0;
  for (const auto& change : diff.staves) {
    transforms += static_cast<int>(change.transforms.size());
  }
  if (transforms > 0) {
    parts.push_back(fmt::format("{} transformation{}", transforms, transforms == 1 ? "" : "s"));
  }

  if (parts.empty()) parts.push_back("no musical change");
  return parts;
}

std::string join(const std::vector<std::string>& parts, const Style& style) {
  const std::string separator = style.unicode ? " · " : " | ";
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += separator;
    out += parts[i];
  }
  return out;
}

std::string describe_transform(const diff::Transform& transform) {
  switch (transform.kind) {
    case diff::Transform::Kind::Transposition:
      return fmt::format("transposed {}", score::semitone_delta(transform.semitones));
    case diff::Transform::Kind::Augmentation:
      return fmt::format("augmented x{}", transform.ratio_num);
    case diff::Transform::Kind::Diminution:
      return fmt::format("diminished /{}", transform.ratio_den);
    case diff::Transform::Kind::Retrograde:
      return "reversed (retrograde)";
    case diff::Transform::Kind::Inversion:
      return "inverted";
  }
  return {};
}

void print_commit_header(const LogEntry& entry, const Style& style) {
  fmt::print("{}\n", yellow(style, fmt::format("commit {}", entry.commit.oid)));
  fmt::print("Author: {}\n", entry.commit.author);
  fmt::print("Date:   {}\n", short_date(entry.commit.date));
  fmt::print("\n    {}\n", entry.commit.subject);
}

/// The per-instrument breakdown under a commit header.
void print_detail_body(const LogEntry& entry, const Style& style) {
  if (entry.is_root) {
    fmt::print("\n  {} instrument{} {} {} measures {} {} notes\n", entry.instrument_count,
               entry.instrument_count == 1 ? "" : "s", style.unicode ? "·" : "|",
               entry.measure_count, style.unicode ? "·" : "|", entry.note_count);
    return;
  }

  if (!entry.diff) {
    if (!entry.note.empty()) fmt::print("\n  {}\n", dim(style, entry.note));
    return;
  }

  const auto& diff = *entry.diff;
  if (diff.empty()) {
    fmt::print("\n  {}\n", dim(style, "no musical change"));
    return;
  }

  if (const auto range = touched_range(diff)) {
    fmt::print("\n{}\n", dim(style, measure_range(style, range->first, range->second)));
  }
  fmt::print("\n");

  if (diff.stats.measures_added > 0) {
    fmt::print("  {}\n", green(style, fmt::format("+ {} measures", diff.stats.measures_added)));
  }
  if (diff.stats.measures_removed > 0) {
    fmt::print("  {}\n", red(style, fmt::format("- {} measures", diff.stats.measures_removed)));
  }

  for (const auto& change : diff.orchestration.changes) {
    switch (change.kind) {
      case diff::PartChange::Kind::Added:
        fmt::print("  {}\n", green(style, fmt::format("+ {}", change.name)));
        break;
      case diff::PartChange::Kind::Removed:
        fmt::print("  {}\n", red(style, fmt::format("- {}", change.name)));
        break;
      case diff::PartChange::Kind::Renamed:
        fmt::print("  {} {} {}\n", change.old_name, glyph_arrow_change(style), change.name);
        break;
    }
  }

  // Deleting bars touches every instrument playing at that point, so most staves have
  // nothing to add beyond the score-wide measure line above. Those are collapsed into a
  // single list rather than repeating the same range a dozen times.
  std::vector<std::string> structural_only;

  for (const auto& change : diff.staves) {
    std::vector<std::string> facts;
    for (const auto& transform : change.transforms) {
      facts.push_back(describe_transform(transform));
    }

    if (facts.empty()) {
      if (change.notes_changed > 0) {
        facts.push_back(fmt::format("{} note{} changed", change.notes_changed,
                                    change.notes_changed == 1 ? "" : "s"));
      }
      if (change.notes_added > 0) {
        facts.push_back(fmt::format("{} note{} added", change.notes_added,
                                    change.notes_added == 1 ? "" : "s"));
      }
      if (change.notes_removed > 0) {
        facts.push_back(fmt::format("{} note{} removed", change.notes_removed,
                                    change.notes_removed == 1 ? "" : "s"));
      }
      if (change.articulations_changed > 0) {
        facts.push_back(fmt::format("{} articulation{} changed", change.articulations_changed,
                                    change.articulations_changed == 1 ? "" : "s"));
      }
    }

    if (facts.empty()) {
      structural_only.push_back(change.staff_label);
      continue;
    }

    fmt::print("\n  {}\n", bold(style, change.staff_label));
    fmt::print("    {}\n",
               dim(style, measure_range(style, change.first_measure, change.last_measure)));
    for (const auto& fact : facts) {
      fmt::print("    {}\n", fact);
    }
  }

  if (!structural_only.empty()) {
    fmt::print("\n  {}\n", dim(style, "also affected, by the measure change alone:"));
    for (const auto& row :
         wrap_list(structural_only, static_cast<std::size_t>(style.width) - 4)) {
      fmt::print("    {}\n", dim(style, row));
    }
  }

  for (const auto& change : diff.key_sigs) {
    fmt::print("\n  {}\n", bold(style, "Key signature"));
    fmt::print("    {} {} {}\n", score::key_signature_name(change.before),
               glyph_arrow_change(style), score::key_signature_name(change.after));
    fmt::print("    {}\n", dim(style, score::key_signature_candidates(change.after)));
  }
}

}  // namespace

void render_log(const std::vector<LogEntry>& entries, const Style& style) {
  if (entries.empty()) {
    fmt::print("No commits touch a score file.\n");
    return;
  }

  for (const auto& entry : entries) {
    fmt::print("{}  {}\n", yellow(style, entry.commit.short_oid()), entry.commit.subject);
    fmt::print("        {}\n\n", dim(style, join(summary_parts(entry, style), style)));
  }
}

void render_log_detail(const std::vector<LogEntry>& entries, const Style& style) {
  if (entries.empty()) {
    fmt::print("No commits touch a score file.\n");
    return;
  }

  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) fmt::print("\n{}\n\n", rule(style));
    print_commit_header(entries[i], style);
    print_detail_body(entries[i], style);
    fmt::print("\n");
  }
}

}  // namespace motif::render
