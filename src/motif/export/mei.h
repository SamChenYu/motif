#pragma once

#include <string>
#include <vector>

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

/// One bar of a score made from bars of other scores (to_mei with bars, below).
struct Bar {
  const score::Score* score = nullptr;  ///< the score the bar is taken from
  int measure = 0;                      ///< the measure's index in each of its staves
  /// For each staff written (the layout's, in order), the index of the staff of `score`
  /// it is taken from, or -1 to leave it empty in this bar.
  std::vector<int> staves;
  int number = 1;           ///< the measure's number: what stave's marks and --bar find it by
  std::string label;        ///< what to call it on screen, if not its number ("" for that)
  std::string text;         ///< words above the top staff at its start ("" for none)
  bool dashed_end = false;  ///< a dashed barline after it
};

/// Writes a score made of bars taken from one or more scores, in order, on the staves of
/// `layout` (their names, braces and order). Key and time signatures and clefs are written
/// wherever they differ from the bar before, so bars can come from different versions.
/// Returns "" if any bar names a measure its score doesn't have.
[[nodiscard]] std::string to_mei(const score::Score& layout, const std::vector<Bar>& bars);

/// An exact time in quarter notes, as num/den in lowest terms.
struct Quarters {
  long long num = 0;
  long long den = 1;

  void add(long long n, long long d);
  [[nodiscard]] std::string text() const;  ///< "3/2", or "2" for a whole number

  friend bool operator==(const Quarters&, const Quarters&) = default;
};

/// When each element of `voice` starts in the MEI to_mei writes, in quarter notes from
/// the barline: exactly what Verovio, and so stave, reads.
///
/// motif's ticks are whole numbers, so a septuplet's notes are each a fraction of a
/// tick short and later onsets drift; these are worked out from the written values and
/// tuplet ratios instead.
[[nodiscard]] std::vector<Quarters> onsets(const score::Voice& voice, int division);

}  // namespace motif::mei
