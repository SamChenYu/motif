#include "motif/render/notation.h"

#include <fmt/format.h>

#include "motif/score/pitch.h"

namespace motif::render {

std::string chord_name(const std::vector<score::Note>& notes) {
  std::string out;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (i > 0) out += '+';
    out += score::note_name(notes[i].pitch);
  }
  return out;
}

std::vector<std::string> note_rows(const Style& style, const std::vector<score::Note>& notes,
                                   const std::vector<int>& measure_of,
                                   const std::vector<int>& chord_of, std::size_t indent) {
  std::vector<std::string> rows;
  if (notes.empty()) return rows;

  // Leave room for the indent and a little slack, so a row never wraps in the terminal.
  const std::size_t limit =
      static_cast<std::size_t>(style.width) > indent + 8
          ? static_cast<std::size_t>(style.width) - indent - 2
          : 40;

  std::string row;
  int current_measure = measure_of.empty() ? 0 : measure_of.front();

  const auto flush = [&]() {
    if (!row.empty()) rows.push_back(row);
    row.clear();
  };

  for (std::size_t i = 0; i < notes.size();) {
    const int measure = i < measure_of.size() ? measure_of[i] : current_measure;

    // Gather every note belonging to this chord, so they print as one token.
    std::size_t end = i + 1;
    if (i < chord_of.size()) {
      while (end < notes.size() && end < chord_of.size() && chord_of[end] == chord_of[i]) {
        ++end;
      }
    }

    std::string token;
    if (measure != current_measure) {
      token = "| ";
      current_measure = measure;
    }
    for (std::size_t n = i; n < end; ++n) {
      if (n > i) token += '+';
      token += score::note_name(notes[n].pitch);
    }

    if (!row.empty() && display_width(row) + 1 + display_width(token) > limit) {
      flush();
    }
    if (!row.empty()) row += ' ';
    row += token;

    i = end;
  }

  flush();
  return rows;
}

}  // namespace motif::render
