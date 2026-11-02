#pragma once

#include <string>
#include <string_view>

#include "motif/score/model.h"

namespace motif::score {

/// The written accidental for a tonal pitch class, in semitones.
/// -2 double flat, -1 flat, 0 natural, +1 sharp, +2 double sharp.
[[nodiscard]] int tpc_alter(int tpc);

/// The letter name for a tonal pitch class: one of A-G.
[[nodiscard]] char tpc_letter(int tpc);

/// The written octave for a pitch, in scientific pitch notation (middle C is C4).
///
/// Derived from the tpc's accidental as well as the MIDI number, so an enharmonic
/// spelling lands in the octave it is *written* in: B#3 sounds as MIDI 60 but is written
/// in octave 3, not 4.
[[nodiscard]] int written_octave(Pitch p);

/// A spelled note name such as "F#5", "Bb3" or "C4".
///
/// Spelling comes from the tpc rather than the MIDI number, so a score written with
/// F sharps never prints G flats.
[[nodiscard]] std::string note_name(Pitch p);

/// The note name without its octave: "F#", "Bb", "C".
[[nodiscard]] std::string pitch_class_name(int tpc);

/// Describes a signed semitone interval as "+2 semitones" / "-1 semitone".
[[nodiscard]] std::string semitone_delta(int semitones);

/// Names the accidental count of a key signature, e.g. "3 flats", "1 sharp", "no accidentals".
[[nodiscard]] std::string key_signature_name(int accidentals);

/// The two keys a signature could denote, as "D minor / F major".
///
/// A key signature fixes only the accidentals; the relative major and minor share one.
/// Choosing between them requires analysing note content, which motif deliberately does
/// not do, so both are reported.
[[nodiscard]] std::string key_signature_candidates(int accidentals);

}  // namespace motif::score
