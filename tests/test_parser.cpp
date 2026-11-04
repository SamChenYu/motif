#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "motif/mscz/archive.h"
#include "motif/mscz/parser.h"
#include "motif/score/pitch.h"

using motif::mscz::parse_mscx;
using motif::score::ElementKind;

namespace {

/// A small MuseScore document exercising the structures the diff engine depends on:
/// a multi-staff part, tuplets, ties, articulations, a mid-score meter change, a second
/// voice starting off the barline, and an irregular measure.
constexpr const char* kTestScore = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<museScore version="4.70">
  <Score>
    <Division>480</Division>
    <metaTag name="workTitle">Test Score</metaTag>
    <metaTag name="composer">Anon.</metaTag>
    <Part id="1">
      <Staff/>
      <trackName>Violin, Violin I</trackName>
      <Instrument id="violin">
        <longName>Violin, Violin I</longName>
        <shortName>Vln. I</shortName>
        <trackName>Violin</trackName>
        <instrumentId>strings.violin</instrumentId>
      </Instrument>
    </Part>
    <Part id="2">
      <Staff/>
      <Staff/>
      <trackName>Piano, Piano</trackName>
      <Instrument id="piano">
        <longName>Piano</longName>
        <trackName>Piano</trackName>
        <instrumentId>keyboard.piano</instrumentId>
        <clef staff="2">F</clef>
      </Instrument>
    </Part>

    <Staff id="1">
      <Measure>
        <eid>MEAS1</eid>
        <voice>
          <KeySig><eid>K1</eid><concertKey>-1</concertKey></KeySig>
          <TimeSig><eid>T1</eid><sigN>4</sigN><sigD>4</sigD></TimeSig>
          <Chord>
            <eid>C1</eid>
            <durationType>quarter</durationType>
            <Articulation><subtype>articStaccatoAbove</subtype></Articulation>
            <Note><eid>N1</eid><pitch>62</pitch><tpc>16</tpc><velocity>80</velocity></Note>
          </Chord>
          <Chord>
            <eid>C2</eid>
            <dots>1</dots>
            <durationType>quarter</durationType>
            <Note>
              <eid>N2</eid><pitch>65</pitch><tpc>13</tpc>
              <Spanner type="Tie"><Tie/><next><location><fractions>3/8</fractions></location></next></Spanner>
            </Note>
          </Chord>
          <Rest><eid>R1</eid><durationType>eighth</durationType></Rest>
          <Tuplet><eid>TU1</eid><normalNotes>2</normalNotes><actualNotes>3</actualNotes><baseNote>eighth</baseNote></Tuplet>
          <Chord><eid>C3</eid><durationType>eighth</durationType><Note><eid>N3</eid><pitch>67</pitch><tpc>15</tpc></Note></Chord>
          <Chord><eid>C4</eid><durationType>eighth</durationType><Note><eid>N4</eid><pitch>69</pitch><tpc>17</tpc></Note></Chord>
          <Chord><eid>C5</eid><durationType>eighth</durationType><Note><eid>N5</eid><pitch>71</pitch><tpc>19</tpc></Note></Chord>
          <endTuplet/>
        </voice>
      </Measure>
      <Measure>
        <eid>MEAS2</eid>
        <voice>
          <TimeSig><eid>T2</eid><sigN>3</sigN><sigD>4</sigD></TimeSig>
          <Rest><eid>R2</eid><durationType>measure</durationType><duration>3/4</duration></Rest>
        </voice>
        <voice>
          <location><fractions>1/4</fractions></location>
          <Chord>
            <eid>C6</eid>
            <durationType>quarter</durationType>
            <Note><eid>N6</eid><pitch>60</pitch><tpc>14</tpc></Note>
            <Note><eid>N7</eid><pitch>64</pitch><tpc>18</tpc></Note>
          </Chord>
        </voice>
      </Measure>
      <Measure len="1/4">
        <eid>MEAS3</eid>
        <voice>
          <Chord><eid>C7</eid><durationType>quarter</durationType><Note><eid>N8</eid><pitch>74</pitch><tpc>16</tpc></Note></Chord>
        </voice>
      </Measure>
    </Staff>

    <Staff id="2">
      <Measure><voice><Rest><eid>PR1</eid><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>
      <Measure><voice><Rest><eid>PR2</eid><durationType>measure</durationType><duration>3/4</duration></Rest></voice></Measure>
      <Measure len="1/4"><voice><Rest><eid>PR3</eid><durationType>quarter</durationType></Rest></voice></Measure>
    </Staff>

    <Staff id="3">
      <Measure><voice><Clef><concertClefType>F</concertClefType><transposingClefType>F</transposingClefType></Clef><Rest><eid>PL1</eid><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>
      <Measure><voice><Rest><eid>PL2</eid><durationType>measure</durationType><duration>3/4</duration></Rest></voice></Measure>
      <Measure len="1/4"><voice><Rest><eid>PL3</eid><durationType>quarter</durationType></Rest></voice></Measure>
    </Staff>
  </Score>
</museScore>)XML";

}  // namespace

TEST_CASE("parses score-level structure", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  CHECK(score->division == 480);
  CHECK(score->title() == "Test Score");
  CHECK(score->meta.at("composer") == "Anon.");
  CHECK(score->parts.size() == 2);
  CHECK(score->staves.size() == 3);
  CHECK(score->measure_count() == 3);
}

TEST_CASE("staves are assigned to parts positionally", "[parser]") {
  // Part/Staff elements carry no id in MuseScore 4 files, so ownership follows document
  // order: a two-staff piano claims the next two score-level staves.
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  CHECK(score->parts[0].staff_ids == std::vector<int>{1});
  CHECK(score->parts[1].staff_ids == std::vector<int>{2, 3});

  CHECK(score->staff_by_id(1)->part_index == 0);
  CHECK(score->staff_by_id(2)->part_index == 1);
  CHECK(score->staff_by_id(3)->part_index == 1);
  CHECK(score->staff_by_id(2)->index_in_part == 0);
  CHECK(score->staff_by_id(3)->index_in_part == 1);

  CHECK(score->part_for_staff(3)->display_name() == "Piano");
}

TEST_CASE("part labels keep the half that distinguishes desks", "[parser]") {
  // "Violin, Violin I" names a family and then the specific desk; only the second half
  // separates the first violins from the seconds.
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  CHECK(score->parts[0].display_name() == "Violin I");
  CHECK(score->parts[0].track_name == "Violin, Violin I");
  CHECK(score->parts[0].instrument_name == "Violin");
  CHECK(score->parts[0].instrument_id == "strings.violin");
  CHECK(score->parts[0].short_name == "Vln. I");

  CHECK(score->parts[1].display_name() == "Piano");
}

TEST_CASE("elements are laid out on a tick grid", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& m1 = score->staff_by_id(1)->measures[0];
  REQUIRE(m1.voices.size() == 1);
  const auto& els = m1.voices[0].elements;
  REQUIRE(els.size() == 6);

  // quarter, dotted quarter, eighth rest, then three triplet eighths of 160 ticks.
  CHECK(els[0].ticks == 480);
  CHECK(els[1].ticks == 720);
  CHECK(els[2].ticks == 240);
  CHECK(els[3].ticks == 160);
  CHECK(els[4].ticks == 160);
  CHECK(els[5].ticks == 160);

  CHECK(els[0].onset == 0);
  CHECK(els[1].onset == 480);
  CHECK(els[2].onset == 1200);
  CHECK(els[3].onset == 1440);
  CHECK(els[4].onset == 1600);
  CHECK(els[5].onset == 1760);

  // The triplet closes the bar exactly at 4/4.
  CHECK(els.back().end_tick() == 1920);
}

TEST_CASE("tuplet membership is recorded", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& els = score->staff_by_id(1)->measures[0].voices[0].elements;
  CHECK_FALSE(els[0].in_tuplet);
  CHECK_FALSE(els[2].in_tuplet);
  CHECK(els[3].in_tuplet);
  CHECK(els[5].in_tuplet);
}

TEST_CASE("tuplet ratios and written note values are kept for engraving", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& voice = score->staff_by_id(1)->measures[0].voices[0];
  REQUIRE(voice.tuplets.size() == 1);
  CHECK(voice.tuplets[0].actual == 3);
  CHECK(voice.tuplets[0].normal == 2);
  CHECK(voice.tuplets[0].parent == -1);

  CHECK(voice.elements[2].tuplet == -1);
  CHECK(voice.elements[3].tuplet == 0);
  CHECK(voice.elements[5].tuplet == 0);

  CHECK(voice.elements[0].duration_type == "quarter");
  CHECK(voice.elements[3].duration_type == "eighth");
  CHECK(score->staff_by_id(2)->measures[0].voices[0].elements[0].duration_type == "measure");
}

TEST_CASE("each staff starts in its instrument's clef", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  CHECK(score->staff_by_id(1)->default_clef == "G");
  CHECK(score->staff_by_id(2)->default_clef == "G");
  CHECK(score->staff_by_id(3)->default_clef == "F");
}

TEST_CASE("a staff's own default clef beats its instrument's", "[parser]") {
  const auto score = parse_mscx(R"XML(<museScore version="4.70"><Score><Division>480</Division>
    <Part id="1">
      <Staff><defaultConcertClef>F8vb</defaultConcertClef><defaultTransposingClef>F</defaultTransposingClef></Staff>
      <trackName>Contrabass</trackName>
      <Instrument id="contrabass"><clef>F</clef></Instrument>
    </Part>
    <Staff id="1"><Measure><voice><Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure></Staff>
  </Score></museScore>)XML");
  REQUIRE(score.has_value());
  CHECK(score->staves[0].default_clef == "F8vb");
}

TEST_CASE("grace notes take no time in the bar", "[parser]") {
  const auto score = parse_mscx(R"XML(<museScore version="4.70"><Score><Division>480</Division>
    <Part id="1"><Staff/><trackName>Flute</trackName></Part>
    <Staff id="1"><Measure><voice>
      <Chord><acciaccatura/><durationType>eighth</durationType><Note><pitch>74</pitch><tpc>16</tpc></Note></Chord>
      <Chord><durationType>half</durationType><Note><pitch>72</pitch><tpc>14</tpc></Note></Chord>
      <Chord><grace16/><durationType>16th</durationType><Note><pitch>71</pitch><tpc>19</tpc></Note></Chord>
      <Chord><durationType>half</durationType><Note><pitch>72</pitch><tpc>14</tpc></Note></Chord>
    </voice></Measure></Staff>
  </Score></museScore>)XML");
  REQUIRE(score.has_value());

  const auto& els = score->staves[0].measures[0].voices[0].elements;
  REQUIRE(els.size() == 2);
  CHECK(els[0].onset == 0);
  CHECK(els[1].onset == 960);
  CHECK(els[1].end_tick() == 1920);
}

TEST_CASE("chords, rests, articulations and ties are distinguished", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& els = score->staff_by_id(1)->measures[0].voices[0].elements;

  CHECK(els[0].kind == ElementKind::Chord);
  CHECK(els[0].eid == "C1");
  CHECK(els[0].articulations == std::vector<std::string>{"articStaccatoAbove"});
  REQUIRE(els[0].notes.size() == 1);
  CHECK(els[0].notes[0].eid == "N1");
  CHECK(els[0].notes[0].velocity == 80);
  CHECK(motif::score::note_name(els[0].notes[0].pitch) == "D4");

  CHECK(els[1].dots == 1);
  CHECK(els[1].notes[0].tie_out);
  CHECK_FALSE(els[1].notes[0].tie_in);

  CHECK(els[2].kind == ElementKind::Rest);
  CHECK(els[2].notes.empty());
}

TEST_CASE("a mid-score meter change resizes the measure", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& m2 = score->staff_by_id(1)->measures[1];
  REQUIRE(m2.time_sig.has_value());
  CHECK(m2.time_sig->n == 3);
  CHECK(m2.time_sig->d == 4);
  CHECK(m2.start_tick == 1920);

  // A whole-measure rest takes its length from the measure, not from a note value.
  CHECK(m2.voices[0].elements[0].ticks == 1440);
}

TEST_CASE("a second voice can start away from the barline", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& m2 = score->staff_by_id(1)->measures[1];
  REQUIRE(m2.voices.size() == 2);
  CHECK(m2.voices[1].index == 2);

  const auto& el = m2.voices[1].elements.at(0);
  CHECK(el.onset == 480);  // shifted by the <location> of 1/4
  CHECK(el.notes.size() == 2);  // a two-note chord
}

TEST_CASE("an irregular measure keeps its explicit length", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& m3 = score->staff_by_id(1)->measures[2];
  CHECK(m3.irregular);
  CHECK(m3.length == motif::score::Fraction(1, 4));
  CHECK(m3.start_tick == 1920 + 1440);
}

TEST_CASE("key signatures come from non-transposing staves", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  const auto& ks = score->staff_by_id(1)->measures[0].key_sig;
  REQUIRE(ks.has_value());
  CHECK(ks->concert == -1);
  CHECK_FALSE(ks->transposing());

  const auto key = score->key_signature(1);
  REQUIRE(key.has_value());
  CHECK(*key == -1);
}

TEST_CASE("measure eids are captured for alignment", "[parser]") {
  const auto score = parse_mscx(kTestScore);
  REQUIRE(score.has_value());

  CHECK(score->staff_by_id(1)->measures[0].eid == "MEAS1");
  CHECK(score->staff_by_id(1)->measures[2].eid == "MEAS3");
}

TEST_CASE("malformed input is rejected with a message, not a crash", "[parser]") {
  std::string error;

  CHECK_FALSE(parse_mscx("not xml at all", &error).has_value());
  CHECK_FALSE(error.empty());

  error.clear();
  CHECK_FALSE(parse_mscx("<museScore></museScore>", &error).has_value());
  CHECK_FALSE(error.empty());

  error.clear();
  CHECK_FALSE(parse_mscx("<museScore><Score><Division>480</Division></Score></museScore>", &error)
                  .has_value());
  CHECK(error == "score contains no staves");
}

TEST_CASE("an uncompressed .mscx is accepted directly", "[archive]") {
  std::string error;
  const auto xml = motif::mscz::extract_mscx(kTestScore, &error);
  REQUIRE(xml.has_value());
  CHECK(xml->find("<museScore") != std::string::npos);

  CHECK_FALSE(motif::mscz::looks_like_zip(kTestScore));
  CHECK_FALSE(motif::mscz::extract_mscx("", &error).has_value());
  CHECK_FALSE(motif::mscz::extract_mscx("random bytes", &error).has_value());
}

#ifdef MOTIF_SAMPLE_SCORE
TEST_CASE("the sample concerto parses to its known dimensions", "[parser][sample]") {
  const std::filesystem::path path{MOTIF_SAMPLE_SCORE};
  if (!std::filesystem::exists(path)) {
    SKIP("sample score not present");
  }

  std::string error;
  const auto score = motif::mscz::parse_score_file(path, &error);
  INFO("parse error: " << error);
  REQUIRE(score.has_value());

  CHECK(score->division == 480);
  CHECK(score->parts.size() == 13);
  CHECK(score->staves.size() == 14);
  CHECK(score->measure_count() == 102);
  CHECK(score->note_count() == 5430);

  // Barlines are score-wide: every staff must hold the same number of measures, which
  // is what lets measure alignment be computed once and applied to all staves.
  for (const auto& staff : score->staves) {
    CHECK(staff.measures.size() == 102);
  }

  // Only staff 1 carries measure eids, so alignment reads them from there.
  int with_eid = 0;
  for (const auto& m : score->staves.front().measures) {
    if (!m.eid.empty()) ++with_eid;
  }
  CHECK(with_eid == 102);

  // The piano is the one part spanning two staves.
  const auto* piano = score->part_for_staff(8);
  REQUIRE(piano != nullptr);
  CHECK(piano->display_name() == "Piano");
  CHECK(piano->staff_ids == std::vector<int>{8, 9});

  // Non-transposing staves agree on one sharp.
  const auto key = score->key_signature(1);
  REQUIRE(key.has_value());
  CHECK(*key == 1);
}
#endif
