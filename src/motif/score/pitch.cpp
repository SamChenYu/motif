#include "motif/score/pitch.h"

#include <fmt/format.h>

#include <array>

namespace motif::score {
namespace {

/// Floor division and modulo. C++ truncates toward zero, which would give the wrong
/// letter and accidental for the double-flat end of the line of fifths (tpc -1 = Fbb).
constexpr int floor_div(int a, int b) {
  const int q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

constexpr int floor_mod(int a, int b) {
  const int r = a % b;
  return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}

/// The line of fifths, in letter order starting from the flat end.
/// Successive tpc values step by a fifth: ... F C G D A E B ...
constexpr std::string_view kLetters = "FCGDAEB";

/// Major keys indexed by accidental count, from 7 flats to 7 sharps.
constexpr std::array<std::string_view, 15> kMajorKeys{
    "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#"};

/// The relative minor of each entry above -- the same signature, a minor third down.
constexpr std::array<std::string_view, 15> kMinorKeys{
    "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#"};

std::string accidental_string(int alter) {
  switch (alter) {
    case -2: return "bb";
    case -1: return "b";
    case 0: return "";
    case 1: return "#";
    case 2: return "##";
    default: return alter < 0 ? std::string(static_cast<std::size_t>(-alter), 'b')
                              : std::string(static_cast<std::size_t>(alter), '#');
  }
}

}  // namespace

int tpc_alter(int tpc) { return floor_div(tpc + 1, 7) - 2; }

char tpc_letter(int tpc) {
  return kLetters[static_cast<std::size_t>(floor_mod(tpc + 1, 7))];
}

int written_octave(Pitch p) {
  // Subtracting the accidental maps the note back to its natural, whose octave is the
  // one the notehead sits in. Without this, B#3 (MIDI 60) would be reported as octave 4.
  return floor_div(p.midi - tpc_alter(p.tpc), 12) - 1;
}

std::string pitch_class_name(int tpc) {
  return fmt::format("{}{}", tpc_letter(tpc), accidental_string(tpc_alter(tpc)));
}

std::string note_name(Pitch p) {
  return fmt::format("{}{}", pitch_class_name(p.tpc), written_octave(p));
}

std::string semitone_delta(int semitones) {
  const int magnitude = semitones < 0 ? -semitones : semitones;
  return fmt::format("{}{} semitone{}", semitones < 0 ? "-" : "+", magnitude,
                     magnitude == 1 ? "" : "s");
}

std::string key_signature_name(int accidentals) {
  if (accidentals == 0) return "no accidentals";
  const int magnitude = accidentals < 0 ? -accidentals : accidentals;
  return fmt::format("{} {}{}", magnitude, accidentals < 0 ? "flat" : "sharp",
                     magnitude == 1 ? "" : "s");
}

std::string key_signature_candidates(int accidentals) {
  if (accidentals < -7 || accidentals > 7) return {};
  const auto i = static_cast<std::size_t>(accidentals + 7);
  return fmt::format("{} minor / {} major", kMinorKeys[i], kMajorKeys[i]);
}

}  // namespace motif::score
