#include <catch2/catch_test_macros.hpp>

#include "motif/render/notation.h"
#include "motif/render/terminal.h"
#include "motif/score/model.h"

using motif::render::display_width;
using motif::render::measure_range;
using motif::render::note_rows;
using motif::render::pad_right;
using motif::render::Style;
using motif::render::wrap_list;

namespace {

Style plain(int width = 80) {
  Style style;
  style.color = false;
  style.unicode = true;
  style.width = width;
  return style;
}

Style ascii(int width = 80) {
  Style style = plain(width);
  style.unicode = false;
  return style;
}

motif::score::Note note(int midi, int tpc) {
  motif::score::Note n;
  n.pitch = {midi, tpc};
  return n;
}

}  // namespace

TEST_CASE("colour helpers are inert when colour is off", "[terminal]") {
  const auto style = plain();
  CHECK(motif::render::bold(style, "x") == "x");
  CHECK(motif::render::red(style, "x") == "x");
  CHECK(motif::render::dim(style, "x") == "x");

  Style coloured = plain();
  coloured.color = true;
  CHECK(motif::render::bold(coloured, "x") == "\x1b[1mx\x1b[0m");
}

TEST_CASE("display width counts characters, not bytes", "[terminal]") {
  // Padding is computed against terminal columns, so multi-byte glyphs must not be
  // counted once per byte or every column would drift.
  CHECK(display_width("abc") == 3);
  CHECK(display_width("─") == 1);
  CHECK(display_width("mm. 84–97") == 9);
  CHECK(display_width("") == 0);
}

TEST_CASE("padding aligns multi-byte text", "[terminal]") {
  CHECK(pad_right("ab", 5) == "ab   ");
  CHECK(display_width(pad_right("Piano · staff 1", 20)) == 20);
  CHECK(pad_right("already wide", 3) == "already wide");
}

TEST_CASE("measure ranges read naturally", "[terminal]") {
  const auto style = plain();
  CHECK(measure_range(style, 84, 97) == "mm. 84–97");
  CHECK(measure_range(style, 84, 84) == "m. 84");
  CHECK(measure_range(ascii(), 84, 97) == "mm. 84-97");
}

TEST_CASE("the rule spans exactly the terminal width", "[terminal]") {
  CHECK(display_width(motif::render::rule(plain(40))) == 40);
  CHECK(display_width(motif::render::rule(ascii(40))) == 40);
  CHECK(motif::render::rule(ascii(5)) == "-----");
}

TEST_CASE("ascii mode emits no bytes above 127", "[terminal]") {
  const auto style = ascii();
  for (const auto text : {motif::render::glyph_rule(style), motif::render::glyph_arrow_down(style),
                          motif::render::glyph_arrow_change(style),
                          motif::render::glyph_note(style), motif::render::glyph_dash(style)}) {
    for (const char c : text) {
      CHECK(static_cast<unsigned char>(c) < 128);
    }
  }
}

TEST_CASE("long lists wrap instead of overflowing", "[terminal]") {
  const std::vector<std::string> instruments{"Flutes", "Oboes", "Clarinets in Bb", "Bassoons",
                                             "Horns in F", "Trumpets in Bb"};
  const auto rows = wrap_list(instruments, 30);
  REQUIRE(rows.size() > 1);
  for (const auto& row : rows) {
    CHECK(display_width(row) <= 30);
  }

  // Every name survives the wrap.
  std::string joined;
  for (const auto& row : rows) joined += row + " ";
  for (const auto& name : instruments) {
    CHECK(joined.find(name) != std::string::npos);
  }
}

TEST_CASE("note rows draw a barline where the measure changes", "[notation]") {
  const std::vector<motif::score::Note> notes{note(62, 16), note(65, 13), note(69, 17)};
  const std::vector<int> measures{84, 84, 85};

  const auto rows = note_rows(plain(), notes, measures, {}, 4);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0] == "D4 F4 | A4");
}

TEST_CASE("note rows wrap without dropping notes", "[notation]") {
  std::vector<motif::score::Note> notes;
  std::vector<int> measures;
  for (int i = 0; i < 40; ++i) {
    notes.push_back(note(60, 14));
    measures.push_back(1);
  }

  const auto rows = note_rows(plain(40), notes, measures, {}, 4);
  REQUIRE(rows.size() > 1);

  int counted = 0;
  for (const auto& row : rows) {
    CHECK(display_width(row) <= 40);
    std::size_t pos = 0;
    while ((pos = row.find("C4", pos)) != std::string::npos) {
      ++counted;
      pos += 2;
    }
  }
  CHECK(counted == 40);
}

TEST_CASE("an empty sequence produces no rows", "[notation]") {
  CHECK(note_rows(plain(), {}, {}, {}, 4).empty());
}

TEST_CASE("simultaneous notes print as one chord, not as a melody", "[notation]") {
  // A violin double-stop must not read as two successive melody notes.
  const std::vector<motif::score::Note> notes{note(58, 12), note(67, 15), note(62, 16)};
  const std::vector<int> measures{23, 23, 23};
  const std::vector<int> chords{1, 1, 2};  // first two sound together

  const auto rows = note_rows(plain(), notes, measures, chords, 4);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0] == "Bb3+G4 D4");
}

TEST_CASE("chord grouping respects barlines", "[notation]") {
  const std::vector<motif::score::Note> notes{note(60, 14), note(64, 18), note(67, 15)};
  const std::vector<int> measures{1, 1, 2};
  const std::vector<int> chords{1, 1, 2};

  const auto rows = note_rows(plain(), notes, measures, chords, 4);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0] == "C4+E4 | G4");
}

TEST_CASE("chord names join simultaneous pitches", "[notation]") {
  CHECK(motif::render::chord_name({note(60, 14), note(64, 18), note(67, 15)}) == "C4+E4+G4");
  CHECK(motif::render::chord_name({note(60, 14)}) == "C4");
  CHECK(motif::render::chord_name({}).empty());
}
