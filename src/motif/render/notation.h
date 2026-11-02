#pragma once

#include <string>
#include <vector>

#include "motif/render/terminal.h"
#include "motif/score/model.h"

namespace motif::render {

/// Lays out a run of notes as one or more printable rows.
///
/// Barlines are drawn wherever the measure number changes, so a phrase reads the way it
/// is written. Notes sharing a chord are joined with '+', so a violin's double-stop
/// reads as one event rather than as two melody notes. Rows wrap at the terminal width
/// rather than being truncated -- a diff that silently drops notes would be worse than
/// one that takes two lines.
///
/// `measure_of` and `chord_of` must be parallel to `notes`. `chord_of` may be empty, in
/// which case every note is treated as its own event.
[[nodiscard]] std::vector<std::string> note_rows(const Style& style,
                                                 const std::vector<score::Note>& notes,
                                                 const std::vector<int>& measure_of,
                                                 const std::vector<int>& chord_of,
                                                 std::size_t indent);

/// Names a group of simultaneously sounding pitches: "C4" or "C4+E4+G4".
[[nodiscard]] std::string chord_name(const std::vector<score::Note>& notes);

}  // namespace motif::render
