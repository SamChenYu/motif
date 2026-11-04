#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "motif/export/mei.h"
#include "motif/mscz/parser.h"

namespace {

/// A violin and a two-staff piano in F major, covering what engraving must get right:
/// tuplets, dots, ties, accidentals against the key, a voice that starts late, a meter
/// change, a clef change, a whole-bar rest and a pickup-length bar.
constexpr const char* kScore = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<museScore version="4.70">
  <Score>
    <Division>480</Division>
    <metaTag name="workTitle">Engraving Test</metaTag>
    <Part id="1">
      <Staff/>
      <trackName>Violins, Violin I</trackName>
      <Instrument id="violin"><trackName>Violins</trackName></Instrument>
    </Part>
    <Part id="2">
      <Staff/>
      <Staff/>
      <trackName>Piano</trackName>
      <Instrument id="piano"><trackName>Piano</trackName><clef staff="2">F</clef></Instrument>
    </Part>

    <Staff id="1">
      <Measure>
        <voice>
          <KeySig><concertKey>-1</concertKey></KeySig>
          <TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>
          <Chord><durationType>quarter</durationType><Note><pitch>70</pitch><tpc>12</tpc></Note></Chord>
          <Chord><durationType>eighth</durationType><Note><pitch>71</pitch><tpc>19</tpc></Note></Chord>
          <Chord><durationType>eighth</durationType><Note><pitch>71</pitch><tpc>19</tpc></Note></Chord>
          <Chord><dots>1</dots><durationType>quarter</durationType>
            <Articulation><subtype>articStaccatoAbove</subtype></Articulation>
            <Note><pitch>66</pitch><tpc>20</tpc>
              <Spanner type="Tie"><Tie/><next><location><fractions>3/8</fractions></location></next></Spanner>
            </Note>
          </Chord>
          <Rest><durationType>eighth</durationType></Rest>
        </voice>
      </Measure>
      <Measure>
        <voice>
          <TimeSig><sigN>3</sigN><sigD>4</sigD></TimeSig>
          <Tuplet><normalNotes>2</normalNotes><actualNotes>3</actualNotes><baseNote>eighth</baseNote></Tuplet>
          <Chord><durationType>eighth</durationType><Note><pitch>66</pitch><tpc>20</tpc>
            <Spanner type="Tie"><Tie/><prev><location><fractions>-3/8</fractions></location></prev></Spanner>
          </Note></Chord>
          <Chord><durationType>eighth</durationType><Note><pitch>67</pitch><tpc>15</tpc></Note></Chord>
          <Chord><durationType>eighth</durationType><Note><pitch>69</pitch><tpc>17</tpc></Note></Chord>
          <endTuplet/>
          <Chord><durationType>half</durationType><Note><pitch>65</pitch><tpc>13</tpc></Note><Note><pitch>69</pitch><tpc>17</tpc></Note></Chord>
        </voice>
        <voice>
          <location><fractions>1/4</fractions></location>
          <Chord><durationType>half</durationType><Note><pitch>62</pitch><tpc>16</tpc></Note></Chord>
        </voice>
      </Measure>
      <Measure len="1/4">
        <voice>
          <Chord><durationType>quarter</durationType><Note><pitch>72</pitch><tpc>14</tpc></Note></Chord>
        </voice>
      </Measure>
    </Staff>

    <Staff id="2">
      <Measure><voice><Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>
      <Measure><voice><Rest><durationType>measure</durationType><duration>3/4</duration></Rest></voice></Measure>
      <Measure len="1/4"><voice><Rest><durationType>quarter</durationType></Rest></voice></Measure>
    </Staff>

    <Staff id="3">
      <Measure><voice><Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>
      <Measure><voice><Clef><concertClefType>G</concertClefType><transposingClefType>G</transposingClefType></Clef><Rest><durationType>measure</durationType><duration>3/4</duration></Rest></voice></Measure>
      <Measure len="1/4"><voice><Rest><durationType>quarter</durationType></Rest></voice></Measure>
    </Staff>
  </Score>
</museScore>)XML";

struct Engraved {
  pugi::xml_document doc;
  pugi::xml_node root;
};

void engrave(Engraved& out) {
  const auto score = motif::mscz::parse_mscx(kScore);
  REQUIRE(score.has_value());
  const std::string mei = motif::mei::to_mei(*score);
  REQUIRE(out.doc.load_string(mei.c_str()));
  out.root = out.doc.child("mei");
  REQUIRE(out.root);
}

pugi::xml_node select(const Engraved& e, const char* xpath) {
  return e.root.select_node(xpath).node();
}

std::vector<pugi::xml_node> select_all(const Engraved& e, const char* xpath) {
  std::vector<pugi::xml_node> out;
  for (const auto& n : e.root.select_nodes(xpath)) out.push_back(n.node());
  return out;
}

std::string_view attr(pugi::xml_node node, const char* name) {
  return node.attribute(name).as_string();
}

}  // namespace

TEST_CASE("staves, parts and clefs make up the score definition", "[mei]") {
  Engraved e;
  engrave(e);

  CHECK(std::string_view(select(e, "meiHead//title").text().as_string()) == "Engraving Test");
  CHECK(attr(select(e, "//score/scoreDef/keySig"), "sig") == "1f");
  CHECK(attr(select(e, "//score/scoreDef/meterSig"), "count") == "4");

  const auto defs = select_all(e, "//score/scoreDef//staffDef");
  REQUIRE(defs.size() == 3);
  CHECK(attr(defs[0], "n") == "1");
  CHECK(std::string_view(defs[0].child("label").text().as_string()) == "Violin I");
  CHECK(attr(defs[0].child("clef"), "shape") == "G");

  // The piano's two staves share a brace and one label, and the left hand is in bass clef.
  const auto piano = select(e, "//score/scoreDef/staffGrp/staffGrp");
  CHECK(attr(piano, "symbol") == "brace");
  CHECK(std::string_view(piano.child("label").text().as_string()) == "Piano");
  CHECK(attr(defs[2], "n") == "3");
  CHECK(attr(defs[2].child("clef"), "shape") == "F");
  CHECK(attr(defs[2].child("clef"), "line") == "4");
}

TEST_CASE("measures keep motif's numbers, staves and voices", "[mei]") {
  Engraved e;
  engrave(e);

  const auto measures = select_all(e, "//section/measure");
  REQUIRE(measures.size() == 3);
  CHECK(attr(measures[0], "n") == "1");
  CHECK(attr(measures[2], "n") == "3");
  CHECK(attr(measures[2], "metcon") == "false");

  CHECK(select_all(e, "//section/measure[2]/staff[@n='1']/layer").size() == 2);
  CHECK(select(e, "//section/measure[2]/staff[@n='1']/layer[@n='2']"));
  CHECK(select(e, "//section/measure[1]/staff[@n='2']/layer/mRest"));
}

TEST_CASE("note values, dots, ties and articulations are written", "[mei]") {
  Engraved e;
  engrave(e);

  const auto notes = select_all(e, "//section/measure[1]/staff[@n='1']/layer/note");
  REQUIRE(notes.size() == 4);
  CHECK(attr(notes[0], "dur") == "4");
  CHECK(attr(notes[1], "dur") == "8");
  CHECK(attr(notes[3], "dots") == "1");
  CHECK(attr(notes[3], "tie") == "i");
  CHECK(attr(notes[3], "artic") == "stacc");
  CHECK(attr(select(e, "//section/measure[1]/staff[@n='1']/layer/rest"), "dur") == "8");

  CHECK(attr(select(e, "//section/measure[2]/staff[@n='1']/layer[@n='1']/tuplet/note"), "tie") == "t");
}

TEST_CASE("accidentals show only where the key and the bar so far don't imply them", "[mei]") {
  Engraved e;
  engrave(e);

  const auto notes = select_all(e, "//section/measure[1]/staff[@n='1']/layer/note");
  REQUIRE(notes.size() == 4);

  // B flat is in the key: nothing drawn, but its pitch still says flat.
  CHECK(attr(notes[0], "pname") == "b");
  CHECK(attr(notes[0], "oct") == "4");
  CHECK_FALSE(notes[0].attribute("accid"));
  CHECK(attr(notes[0], "accid.ges") == "f");

  // B natural needs its sign once; the second B in the bar inherits it.
  CHECK(attr(notes[1], "accid") == "n");
  CHECK_FALSE(notes[2].attribute("accid"));
  CHECK(attr(notes[2], "accid.ges") == "n");

  CHECK(attr(notes[3], "pname") == "f");
  CHECK(attr(notes[3], "accid") == "s");

  // The tied-over F sharp in the next bar keeps its sharp without redrawing it.
  const auto tied = select(e, "//section/measure[2]/staff[@n='1']/layer[@n='1']/tuplet/note");
  CHECK_FALSE(tied.attribute("accid"));
  CHECK(attr(tied, "accid.ges") == "s");
}

TEST_CASE("tuplets are bracketed and chords grouped", "[mei]") {
  Engraved e;
  engrave(e);

  const auto tuplet = select(e, "//section/measure[2]/staff[@n='1']/layer[@n='1']/tuplet");
  REQUIRE(tuplet);
  CHECK(attr(tuplet, "num") == "3");
  CHECK(attr(tuplet, "numbase") == "2");
  CHECK(select_all(e, "//section/measure[2]/staff[@n='1']/layer[@n='1']/tuplet/note").size() == 3);

  const auto chord = select(e, "//section/measure[2]/staff[@n='1']/layer[@n='1']/chord");
  REQUIRE(chord);
  CHECK(attr(chord, "dur") == "2");
  CHECK(select_all(e, "//section/measure[2]/staff[@n='1']/layer[@n='1']/chord/note").size() == 2);
}

TEST_CASE("a voice that starts late begins with a space", "[mei]") {
  Engraved e;
  engrave(e);

  const auto layer = select(e, "//section/measure[2]/staff[@n='1']/layer[@n='2']");
  REQUIRE(layer);
  const auto first = layer.first_child();
  CHECK(std::string_view(first.name()) == "space");
  CHECK(attr(first, "dur") == "4");
  CHECK(std::string_view(first.next_sibling().name()) == "note");
}

TEST_CASE("meter and clef changes are written where they happen", "[mei]") {
  Engraved e;
  engrave(e);

  // The 3/4 bar is preceded by a scoreDef carrying the new meter.
  const auto change = select(e, "//section/scoreDef");
  REQUIRE(change);
  CHECK(attr(change.child("meterSig"), "count") == "3");
  CHECK(std::string_view(change.next_sibling().name()) == "measure");
  CHECK(attr(change.next_sibling(), "n") == "2");

  // The left hand moves to treble clef in bar 2.
  const auto clef = select(e, "//section/measure[2]/staff[@n='3']/layer/clef");
  REQUIRE(clef);
  CHECK(attr(clef, "shape") == "G");
}
