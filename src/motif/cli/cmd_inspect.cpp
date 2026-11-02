#include <fmt/format.h>

#include <filesystem>
#include <string>

#include "motif/cli/commands.h"
#include "motif/mscz/archive.h"
#include "motif/mscz/parser.h"
#include "motif/score/pitch.h"
#include "motif/score/ticks.h"

namespace motif::cli {
namespace {

namespace fs = std::filesystem;

void print_help() {
  fmt::print(
      "usage: motif inspect <score-file> [--measures A:B] [--staff N]\n"
      "\n"
      "Dumps the parsed structure of a .mscz or .mscx file. A development aid for\n"
      "checking what motif reads out of a score before diffing it.\n");
}

/// Parses "84:104" into a measure range. Returns false when the text is malformed.
bool parse_range(std::string_view text, int& first, int& last) {
  const auto colon = text.find(':');
  if (colon == std::string_view::npos) return false;
  try {
    first = std::stoi(std::string(text.substr(0, colon)));
    last = std::stoi(std::string(text.substr(colon + 1)));
  } catch (const std::exception&) {
    return false;
  }
  return first <= last;
}

void print_summary(const score::Score& s, const fs::path& path) {
  fmt::print("{}\n", path.filename().string());
  if (const auto title = s.title(); !title.empty()) {
    fmt::print("  title            {}\n", title);
  }
  fmt::print("  division         {} ticks/quarter\n", s.division);
  fmt::print("  parts            {}\n", s.parts.size());
  fmt::print("  staves           {}\n", s.staves.size());
  fmt::print("  measures         {}\n", s.measure_count());
  fmt::print("  notes            {}\n", s.note_count());

  // Measure eids are what the diff engine aligns on, so report how many are available.
  int measures_with_eid = 0;
  for (const auto& m : s.staves.front().measures) {
    if (!m.eid.empty()) ++measures_with_eid;
  }
  fmt::print("  measure eids     {}/{} on staff {}\n", measures_with_eid,
             s.staves.front().measures.size(), s.staves.front().id);
  if (const auto key = s.key_signature(1)) {
    fmt::print("  key signature    {} ({})\n", score::key_signature_name(*key),
               score::key_signature_candidates(*key));
  }
  fmt::print("\n");
}

void print_parts(const score::Score& s) {
  fmt::print("PARTS\n");
  for (std::size_t i = 0; i < s.parts.size(); ++i) {
    const auto& p = s.parts[i];
    fmt::print("  [{:>2}] {:<24} {:<26} staves ", i, p.display_name(), p.instrument_id);
    for (std::size_t j = 0; j < p.staff_ids.size(); ++j) {
      fmt::print("{}{}", j == 0 ? "" : ",", p.staff_ids[j]);
    }
    fmt::print("\n");
  }
  fmt::print("\n");
}

void print_measures(const score::Score& s, int staff_filter, int first, int last) {
  for (const auto& staff : s.staves) {
    if (staff_filter > 0 && staff.id != staff_filter) continue;

    const auto* part = s.part_for_staff(staff.id);
    fmt::print("STAFF {} -- {}\n", staff.id, part != nullptr ? part->display_name() : "?");

    for (const auto& m : staff.measures) {
      if (m.number < first || m.number > last) continue;

      fmt::print("  m.{:<4} tick {:<7} {} ticks", m.number, m.start_tick,
                 m.length.to_ticks(s.division));
      if (m.time_sig) fmt::print("  timesig {}/{}", m.time_sig->n, m.time_sig->d);
      if (m.key_sig) {
        // On a transposing staff the stored `concert` value is the written one, so
        // label it as read from the file rather than asserting it is concert pitch.
        if (m.key_sig->transposing()) {
          fmt::print("  keysig {} written / {} sounding (transposing staff)",
                     score::key_signature_name(*m.key_sig->actual),
                     score::key_signature_name(m.key_sig->concert));
        } else {
          fmt::print("  keysig {}", score::key_signature_name(m.key_sig->concert));
        }
      }
      if (m.irregular) fmt::print("  irregular");
      fmt::print("\n");

      for (const auto& voice : m.voices) {
        if (voice.elements.empty()) continue;
        fmt::print("      voice {}: ", voice.index);

        for (const auto& el : voice.elements) {
          if (el.is_rest()) {
            fmt::print("[rest {}] ", score::describe_ticks(el.ticks, s.division));
            continue;
          }
          fmt::print("[");
          for (std::size_t n = 0; n < el.notes.size(); ++n) {
            fmt::print("{}{}", n == 0 ? "" : "+", score::note_name(el.notes[n].pitch));
          }
          fmt::print(" {}", score::describe_ticks(el.ticks, s.division));
          if (el.in_tuplet) fmt::print(" tuplet");
          for (const auto& a : el.articulations) fmt::print(" {}", a);
          fmt::print("] ");
        }
        fmt::print("\n");
      }
    }
    fmt::print("\n");
  }
}

}  // namespace

int cmd_inspect(const std::vector<std::string>& args) {
  fs::path path;
  int staff_filter = 0;
  int first = 1;
  int last = 8;  // a short excerpt by default; a full score is far too long to read
  bool range_given = false;

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "-h" || a == "--help") {
      print_help();
      return 0;
    }
    if (a == "--measures" && i + 1 < args.size()) {
      if (!parse_range(args[++i], first, last)) {
        fmt::print(stderr, "motif: --measures expects a range like 84:104\n");
        return 1;
      }
      range_given = true;
      continue;
    }
    if (a == "--staff" && i + 1 < args.size()) {
      staff_filter = std::stoi(args[++i]);
      continue;
    }
    if (a.starts_with("-")) {
      fmt::print(stderr, "motif inspect: unknown option '{}'\n", a);
      return 1;
    }
    path = a;
  }

  if (path.empty()) {
    print_help();
    return 1;
  }

  std::string error;
  const auto score = mscz::parse_score_file(path, &error);
  if (!score) {
    fmt::print(stderr, "motif: {}: {}\n", path.string(), error);
    return 1;
  }

  print_summary(*score, path);
  print_parts(*score);

  if (!range_given) {
    fmt::print("(showing measures {}-{}; pass --measures A:B for more)\n\n", first, last);
  }
  print_measures(*score, staff_filter, first, last);
  return 0;
}

}  // namespace motif::cli
