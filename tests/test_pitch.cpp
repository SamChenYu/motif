#include <catch2/catch_test_macros.hpp>

#include "motif/score/pitch.h"

using motif::score::key_signature_candidates;
using motif::score::key_signature_name;
using motif::score::note_name;
using motif::score::Pitch;
using motif::score::pitch_class_name;
using motif::score::semitone_delta;
using motif::score::tpc_alter;
using motif::score::written_octave;

TEST_CASE("tpc maps to letter and accidental along the line of fifths", "[pitch]") {
  // The line of fifths runs ... Bb F C G D A E B F# ... with C natural at 14.
  CHECK(pitch_class_name(14) == "C");
  CHECK(pitch_class_name(15) == "G");
  CHECK(pitch_class_name(16) == "D");
  CHECK(pitch_class_name(17) == "A");
  CHECK(pitch_class_name(18) == "E");
  CHECK(pitch_class_name(19) == "B");
  CHECK(pitch_class_name(13) == "F");
  CHECK(pitch_class_name(20) == "F#");
  CHECK(pitch_class_name(12) == "Bb");
  CHECK(pitch_class_name(11) == "Eb");
  CHECK(pitch_class_name(21) == "C#");
}

TEST_CASE("tpc covers the full range present in real scores", "[pitch]") {
  // tpc 12..25 is what the sample concerto uses; the format allows -1..33.
  CHECK(pitch_class_name(22) == "G#");
  CHECK(pitch_class_name(23) == "D#");
  CHECK(pitch_class_name(24) == "A#");
  CHECK(pitch_class_name(25) == "E#");
  CHECK(pitch_class_name(7) == "Cb");
  CHECK(pitch_class_name(-1) == "Fbb");
  CHECK(pitch_class_name(33) == "B##");
}

TEST_CASE("accidental sign follows the tpc", "[pitch]") {
  CHECK(tpc_alter(14) == 0);
  CHECK(tpc_alter(20) == 1);
  CHECK(tpc_alter(12) == -1);
  CHECK(tpc_alter(27) == 2);   // F##
  CHECK(tpc_alter(5) == -2);   // Fbb .. Bbb region
  CHECK(tpc_alter(-1) == -2);  // the flat end of the line must not truncate toward zero
}

TEST_CASE("enharmonic spellings land in the octave they are written in", "[pitch]") {
  // B#3 sounds as MIDI 60 but is notated in octave 3, not 4.
  CHECK(note_name(Pitch{.midi = 60, .tpc = 19 + 7}) == "B#3");
  // Cb4 sounds as MIDI 59 but is notated in octave 4.
  CHECK(note_name(Pitch{.midi = 59, .tpc = 7}) == "Cb4");
  CHECK(written_octave(Pitch{.midi = 60, .tpc = 14}) == 4);
  CHECK(written_octave(Pitch{.midi = 21, .tpc = 17}) == 0);  // A0, lowest piano key
}

TEST_CASE("note names match the sample concerto's pitches", "[pitch]") {
  CHECK(note_name(Pitch{.midi = 91, .tpc = 15}) == "G6");
  CHECK(note_name(Pitch{.midi = 90, .tpc = 20}) == "F#6");
  CHECK(note_name(Pitch{.midi = 60, .tpc = 14}) == "C4");
  CHECK(note_name(Pitch{.midi = 82, .tpc = 12}) == "Bb5");
}

TEST_CASE("semitone deltas read as prose", "[pitch]") {
  CHECK(semitone_delta(2) == "+2 semitones");
  CHECK(semitone_delta(-3) == "-3 semitones");
  CHECK(semitone_delta(1) == "+1 semitone");
  CHECK(semitone_delta(-1) == "-1 semitone");
}

TEST_CASE("key signatures are named by accidental count", "[pitch]") {
  CHECK(key_signature_name(0) == "no accidentals");
  CHECK(key_signature_name(1) == "1 sharp");
  CHECK(key_signature_name(3) == "3 sharps");
  CHECK(key_signature_name(-1) == "1 flat");
  CHECK(key_signature_name(-4) == "4 flats");
}

TEST_CASE("a signature names both keys it could denote", "[pitch]") {
  // A signature fixes the accidentals only; relative major and minor share one, and
  // separating them needs note content that motif deliberately does not analyse.
  CHECK(key_signature_candidates(-1) == "D minor / F major");
  CHECK(key_signature_candidates(1) == "E minor / G major");
  CHECK(key_signature_candidates(0) == "A minor / C major");
  CHECK(key_signature_candidates(-2) == "G minor / Bb major");
  CHECK(key_signature_candidates(4) == "C# minor / E major");
}
