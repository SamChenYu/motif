#pragma once

#include <string>

#include "motif/score/model.h"

namespace motif::mei {

/// Writes a score as MEI, the format Verovio (and so stave) engraves from.
///
/// Everything is written at concert pitch, so each note's MIDI number is the one motif
/// diffed, and the layout follows motif's model exactly: staff n is the score's nth
/// staff from the top, layer n is voice n, and each measure carries motif's own measure
/// number. That is what lets a diff's marks find their notes in stave.
///
/// What motif does not model (dynamics, slurs, lyrics, text, grace notes, ...) is left
/// out; gaps in a voice become invisible spaces so that every note keeps its onset.
[[nodiscard]] std::string to_mei(const score::Score& score);

}  // namespace motif::mei
