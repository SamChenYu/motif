#include "motif/render/diff_view.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>

#include "motif/render/notation.h"
#include "motif/score/pitch.h"

namespace motif::render {
namespace {

/// How many individual note changes to list when a run has no printable sequence.
constexpr std::size_t kMaxListedChanges = 12;

std::string upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

void print_transform(const diff::Transform& transform, const Style& style) {
  std::string text;
  switch (transform.kind) {
    case diff::Transform::Kind::Transposition:
      text = fmt::format("transposed {}", score::semitone_delta(transform.semitones));
      break;
    case diff::Transform::Kind::Augmentation:
      text = fmt::format("augmented x{}", transform.ratio_num);
      break;
    case diff::Transform::Kind::Diminution:
      text = fmt::format("diminished /{}", transform.ratio_den);
      break;
    case diff::Transform::Kind::Retrograde:
      text = "reversed (retrograde)";
      break;
    case diff::Transform::Kind::Inversion:
      text = "inverted";
      break;
  }
  fmt::print("\n  {} {}\n", glyph_arrow_change(style), cyan(style, text));
}

/// Lists individual note changes, for runs whose material could not be shown as a
/// linear sequence (independent voices, or simply too long to print).
void print_change_list(const diff::StaffChange& change, const Style& style) {
  std::size_t shown = 0;
  for (const auto& note : change.notes) {
    if (shown >= kMaxListedChanges) break;

    std::string line;
    switch (note.kind) {
      case diff::NoteChange::Kind::PitchChanged:
        if (!note.before || !note.after) continue;
        line = fmt::format("{} {} {}", score::note_name(*note.before), glyph_arrow_change(style),
                           score::note_name(*note.after));
        break;
      case diff::NoteChange::Kind::Added:
        if (!note.after) continue;
        line = green(style, fmt::format("+ {}", score::note_name(*note.after)));
        break;
      case diff::NoteChange::Kind::Removed:
        if (!note.before) continue;
        line = red(style, fmt::format("- {}", score::note_name(*note.before)));
        break;
      case diff::NoteChange::Kind::DurationChanged:
        if (!note.after) continue;
        line = fmt::format("{} rhythm {} {}", score::note_name(*note.after),
                           glyph_arrow_change(style),
                           note.after_ticks > note.before_ticks ? "lengthened" : "shortened");
        break;
      case diff::NoteChange::Kind::ArticulationChanged:
        line = "articulation changed";
        break;
    }

    fmt::print("    {}  {}\n", dim(style, fmt::format("m. {:<4}", note.measure)), line);
    ++shown;
  }

  const std::size_t total = change.notes.size();
  if (total > shown) {
    fmt::print("    {}\n", dim(style, fmt::format("... and {} more", total - shown)));
  }
}

void print_staff_change(const diff::StaffChange& change, const Style& style) {
  fmt::print("\n{}\n", bold(style, upper(change.staff_label)));
  fmt::print("  {}\n", dim(style, measure_range(style, change.first_measure, change.last_measure)));

  if (change.measures_added > 0 || change.measures_removed > 0) {
    if (change.measures_added > 0) {
      fmt::print("  {}\n", green(style, fmt::format("+ {} measure{}", change.measures_added,
                                                    change.measures_added == 1 ? "" : "s")));
    }
    if (change.measures_removed > 0) {
      fmt::print("  {}\n", red(style, fmt::format("- {} measure{}", change.measures_removed,
                                                  change.measures_removed == 1 ? "" : "s")));
    }
  }

  if (change.has_sequences()) {
    const bool truncated =
        change.sequence_state == diff::StaffChange::SequenceState::Truncated;

    // The note glyph marks the start of a phrase, so it appears once; wrapped
    // continuation rows line up underneath it.
    const std::string glyph(glyph_note(style));
    const std::string lead = glyph.empty() ? "    " : fmt::format("  {} ", glyph);
    const std::size_t indent = display_width(lead);
    const std::string continuation(indent, ' ');

    const auto print_block = [&](const char* heading, const std::vector<score::Note>& notes,
                                 const std::vector<int>& measures,
                                 const std::vector<int>& chords,
                                 std::string (*colour)(const Style&, std::string_view)) {
      const auto rows = note_rows(style, notes, measures, chords, indent);
      if (rows.empty()) return;

      fmt::print("\n  {}\n", dim(style, heading));
      for (std::size_t i = 0; i < rows.size(); ++i) {
        fmt::print("{}{}\n", i == 0 ? lead : continuation, colour(style, rows[i]));
      }
      if (truncated) {
        fmt::print("{}{}\n", continuation, dim(style, "... (run continues)"));
      }
    };

    print_block("BEFORE", change.before_seq, change.before_measures, change.before_chords, &red);
    print_block("AFTER", change.after_seq, change.after_measures, change.after_chords, &green);
  } else if (!change.notes.empty()) {
    fmt::print("\n");
    print_change_list(change, style);
  }

  for (const auto& transform : change.transforms) {
    print_transform(transform, style);
  }

  if (change.articulations_changed > 0) {
    fmt::print("\n  {} articulation{} changed\n", change.articulations_changed,
               change.articulations_changed == 1 ? "" : "s");
  }

  fmt::print("\n");
}

void print_orchestration(const diff::OrchestrationChange& orchestration, const Style& style) {
  if (orchestration.changes.empty()) return;

  fmt::print("\n{}\n\n", bold(style, "ORCHESTRATION"));

  for (const auto& change : orchestration.changes) {
    switch (change.kind) {
      case diff::PartChange::Kind::Removed:
        fmt::print("  {}\n", red(style, fmt::format("- {}", change.name)));
        break;
      case diff::PartChange::Kind::Added:
        fmt::print("  {}\n", green(style, fmt::format("+ {}", change.name)));
        break;
      case diff::PartChange::Kind::Renamed:
        fmt::print("  {} {} {}\n", change.old_name, glyph_arrow_change(style), change.name);
        break;
    }
  }

  if (orchestration.unchanged.empty()) return;

  fmt::print("\n  {}\n", dim(style, "unchanged:"));
  for (const auto& row :
       wrap_list(orchestration.unchanged, static_cast<std::size_t>(style.width) - 6)) {
    fmt::print("    {}\n", dim(style, row));
  }
}

void print_key_signatures(const std::vector<diff::KeySigChange>& changes, const Style& style) {
  if (changes.empty()) return;

  fmt::print("\n{}\n", bold(style, "KEY SIGNATURE"));

  for (const auto& change : changes) {
    // A signature fixes the accidentals but not the mode, so both candidate keys are
    // named rather than guessing between them from the notes.
    fmt::print("\n  {:<9} ({})\n", score::key_signature_name(change.before),
               score::key_signature_candidates(change.before));
    fmt::print("        {}\n", glyph_arrow_down(style));
    fmt::print("  {:<9} ({})\n", score::key_signature_name(change.after),
               score::key_signature_candidates(change.after));
    fmt::print("\n  {}\n", dim(style, fmt::format("at m. {}", change.measure)));

    if (change.all_staves) {
      fmt::print("  {}\n", dim(style, "all staves"));
    } else if (!change.staves.empty()) {
      for (const auto& row : wrap_list(change.staves, static_cast<std::size_t>(style.width) - 4)) {
        fmt::print("  {}\n", dim(style, row));
      }
    }
  }
  fmt::print("\n");
}

void print_time_signatures(const std::vector<diff::TimeSigChange>& changes, const Style& style) {
  if (changes.empty()) return;

  fmt::print("\n{}\n\n", bold(style, "TIME SIGNATURE"));
  for (const auto& change : changes) {
    fmt::print("  {}/{} {} {}/{}   {}\n", change.before.n, change.before.d,
               glyph_arrow_change(style), change.after.n, change.after.d,
               dim(style, fmt::format("at m. {}", change.measure)));
  }
  fmt::print("\n");
}

void print_summary(const diff::ScoreDiff& diff, const Style& style) {
  fmt::print("{}\n", rule(style));

  const auto& stats = diff.stats;
  const auto line = [](int count, const char* singular, const char* plural_form,
                       const char* verb) {
    return fmt::format("{} {} {}", count, count == 1 ? singular : plural_form, verb);
  };

  bool printed = false;
  if (stats.instruments_changed() > 0) {
    fmt::print("{}\n", line(stats.instruments_changed(), "instrument", "instruments", "changed"));
    printed = true;
  }
  if (stats.notes_changed > 0) {
    fmt::print("{}\n", line(stats.notes_changed, "note", "notes", "changed"));
    printed = true;
  }
  if (stats.notes_added > 0) {
    fmt::print("{}\n", line(stats.notes_added, "note", "notes", "added"));
    printed = true;
  }
  if (stats.notes_removed > 0) {
    fmt::print("{}\n", line(stats.notes_removed, "note", "notes", "removed"));
    printed = true;
  }
  if (stats.measures_added > 0) {
    fmt::print("{}\n", line(stats.measures_added, "measure", "measures", "added"));
    printed = true;
  }
  if (stats.measures_removed > 0) {
    fmt::print("{}\n", line(stats.measures_removed, "measure", "measures", "removed"));
    printed = true;
  }
  if (stats.articulations_changed > 0) {
    fmt::print("{}\n", line(stats.articulations_changed, "articulation", "articulations",
                            "changed"));
    printed = true;
  }
  if (!diff.key_sigs.empty()) {
    fmt::print("{}\n", line(static_cast<int>(diff.key_sigs.size()), "key signature",
                            "key signatures", "changed"));
    printed = true;
  }

  int transforms = 0;
  for (const auto& change : diff.staves) {
    transforms += static_cast<int>(change.transforms.size());
  }
  if (transforms > 0) {
    fmt::print("{}\n", line(transforms, "transformation", "transformations", "detected"));
    printed = true;
  }

  if (!printed) fmt::print("no musical change\n");
}

}  // namespace

void render_diff(const diff::ScoreDiff& diff, const Style& style, const std::string& subtitle,
                 const DiffViewOptions& options) {
  if (!diff.title.empty()) {
    fmt::print("{}\n", bold(style, diff.title));
  }
  if (!subtitle.empty()) {
    fmt::print("{}\n", dim(style, subtitle));
  }
  fmt::print("{}\n", rule(style));

  if (diff.empty()) {
    fmt::print("\nNo musical change.\n");
    return;
  }

  if (!options.stat_only) {
    if (options.show_staves) {
      for (const auto& change : diff.staves) {
        print_staff_change(change, style);
      }
    }
    if (options.show_orchestration) {
      print_orchestration(diff.orchestration, style);
    }
    if (options.show_key) {
      print_key_signatures(diff.key_sigs, style);
      print_time_signatures(diff.time_sigs, style);
    }
  }

  fmt::print("\n");
  print_summary(diff, style);
}

}  // namespace motif::render
