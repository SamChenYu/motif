#pragma once

#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace motif::score {

/// An exact musical fraction (measure lengths, time signatures, tuplet ratios).
/// Always stored normalised with a positive denominator.
struct Fraction {
  int num = 0;
  int den = 1;

  constexpr Fraction() = default;
  constexpr Fraction(int n, int d) : num(n), den(d) { normalise(); }

  constexpr void normalise() {
    if (den < 0) {
      num = -num;
      den = -den;
    }
    if (den == 0) {
      den = 1;
      num = 0;
      return;
    }
    const int g = std::gcd(num < 0 ? -num : num, den);
    if (g > 1) {
      num /= g;
      den /= g;
    }
  }

  [[nodiscard]] constexpr bool is_zero() const { return num == 0; }

  /// Ticks this fraction of a *whole note* occupies, given ticks-per-quarter.
  [[nodiscard]] constexpr int to_ticks(int division) const {
    return static_cast<int>((static_cast<long long>(num) * division * 4) / den);
  }

  friend bool operator==(const Fraction& a, const Fraction& b) {
    return a.num == b.num && a.den == b.den;
  }
};

/// A pitch, carried in both of the forms MuseScore stores.
///
/// `midi` is what sounds; `tpc` (tonal pitch class -- a position on the line of fifths)
/// is what is *written*. Only tpc can distinguish F# from Gb, so all spelling goes
/// through it and all interval arithmetic goes through midi.
struct Pitch {
  int midi = 60;
  int tpc = 14;  ///< 14 == C natural

  friend bool operator==(const Pitch& a, const Pitch& b) {
    return a.midi == b.midi && a.tpc == b.tpc;
  }
};

struct Note {
  std::string eid;
  Pitch pitch;
  int velocity = -1;     ///< -1 when the file does not override the instrument default
  bool tie_out = false;  ///< this note begins a tie
  bool tie_in = false;   ///< this note ends a tie
};

enum class ElementKind { Chord, Rest };

/// One rhythmic event in a voice: a chord (one or more noteheads) or a rest.
///
/// Chords and rests share a struct rather than being a variant because every consumer --
/// alignment, note diffing, rendering -- needs uniform access to onset and duration, and
/// only occasionally cares which kind it is holding.
struct Element {
  ElementKind kind = ElementKind::Rest;
  std::string eid;
  int onset = 0;  ///< ticks from the start of the measure
  int ticks = 0;
  std::string duration_type;  ///< MuseScore's written value: "quarter", "16th", "measure", ...
  int dots = 0;
  bool in_tuplet = false;
  int tuplet = -1;  ///< innermost tuplet, as an index into Voice::tuplets; -1 for none
  std::vector<Note> notes;                ///< empty for rests
  std::vector<std::string> articulations;  ///< MuseScore subtype names, sorted

  [[nodiscard]] bool is_chord() const { return kind == ElementKind::Chord; }
  [[nodiscard]] bool is_rest() const { return kind == ElementKind::Rest; }
  [[nodiscard]] int end_tick() const { return onset + ticks; }
};

/// A tuplet bracket: `actual` notes in the time of `normal`, e.g. 3 in the time of 2.
struct Tuplet {
  int actual = 3;
  int normal = 2;
  int parent = -1;  ///< the tuplet this one is nested in, as an index into Voice::tuplets
};

struct Voice {
  int index = 1;  ///< 1-based, matching MuseScore's own voice numbering
  std::vector<Element> elements;
  std::vector<Tuplet> tuplets;  ///< in the order they open
};

struct TimeSig {
  int n = 4;
  int d = 4;
  [[nodiscard]] Fraction as_fraction() const { return Fraction(n, d); }
};

/// A written key signature, as a signed accidental count (-7..7, negative = flats).
///
/// `actual` is present only on transposing staves. Its presence is the reliable signal:
/// a staff carrying no `actual` is non-transposing, so its `concert` value is the score's
/// true concert signature.
///
/// `concert` is *not* dependable on transposing staves. In MIDI-imported scores it holds
/// the written signature rather than the concert one -- in the sample concerto every
/// plain staff reads 1 sharp, while the clarinet and trumpet read 3 and the horn 2,
/// each offset by exactly its own transposition in fifths. Score-level signatures are
/// therefore derived only from staves without an `actual`; see Score::key_signature().
///
/// A signature does not determine a key: one flat is D minor or F major, and nothing but
/// note content separates them. motif reports the signature, never a guess at the mode.
struct KeySig {
  int concert = 0;
  std::optional<int> actual;

  /// True when this staff transposes, making `concert` unreliable.
  [[nodiscard]] bool transposing() const { return actual.has_value(); }
};

struct Clef {
  std::string concert;
  std::string transposing;
};

struct Measure {
  std::string eid;      ///< present only on staff 1 in MuseScore 4 files
  int number = 0;       ///< 1-based display number
  int start_tick = 0;   ///< absolute ticks from the start of the score
  Fraction length{4, 4};
  bool irregular = false;  ///< pickup or otherwise short of its nominal length

  // Present only when the element is declared at this measure; otherwise the value
  // carries over from an earlier measure on the same staff.
  std::optional<TimeSig> time_sig;
  std::optional<KeySig> key_sig;
  std::optional<Clef> clef;

  std::vector<Voice> voices;

  [[nodiscard]] int note_count() const;
};

struct Staff {
  int id = 0;
  int part_index = -1;    ///< index into Score::parts
  int index_in_part = 0;  ///< 0 for the right hand of a piano, 1 for the left, etc.
  /// The clef in force until a measure declares one: the instrument's own, e.g. "F" for
  /// a cello, which MuseScore does not repeat as a <Clef> in the first measure.
  std::string default_clef = "G";
  std::vector<Measure> measures;
};

struct Part {
  std::string id;
  std::string track_name;       ///< the part's own label, e.g. "Violins, Violin I"
  std::string instrument_name;  ///< the instrument's generic label, e.g. "Violins (section)"
  std::string long_name;
  std::string short_name;
  std::string instrument_id;  ///< e.g. "wind.reed.oboe" -- stable across renames
  std::vector<int> staff_ids;

  /// A concise, distinguishing label: "Violin I", "Horns in F", "Timpani".
  ///
  /// MuseScore writes a part's trackName as "family, specific" -- "Violins, Violin I" --
  /// where only the second half separates the first violins from the seconds. The
  /// instrument's own trackName is no good here: it is the generic "Violins (section)"
  /// for both desks.
  [[nodiscard]] std::string display_name() const;
};

struct Score {
  int division = 480;  ///< ticks per quarter note
  std::map<std::string, std::string> meta;
  std::vector<Part> parts;
  std::vector<Staff> staves;

  [[nodiscard]] const Staff* staff_by_id(int id) const;
  [[nodiscard]] const Part* part_for_staff(int staff_id) const;

  /// Measures in the score. Every staff carries the same count, since barlines are
  /// score-wide; this reports the first staff's and 0 for an empty score.
  [[nodiscard]] int measure_count() const;
  [[nodiscard]] int note_count() const;

  /// Title, preferring the workTitle metadata tag.
  [[nodiscard]] std::string title() const;

  /// The concert-pitch key signature in force at `measure_number`, as an accidental
  /// count, or nullopt when no staff declares one.
  ///
  /// Read only from non-transposing staves, whose values are unambiguous, and taken as
  /// the most common such value so that one inconsistent staff cannot skew the result.
  [[nodiscard]] std::optional<int> key_signature(int measure_number) const;
};

}  // namespace motif::score
