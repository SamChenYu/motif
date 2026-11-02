#include <catch2/catch_test_macros.hpp>

#include "motif/score/model.h"
#include "motif/score/ticks.h"

using motif::score::apply_tuplet;
using motif::score::describe_ticks;
using motif::score::duration_ticks;
using motif::score::Fraction;

namespace {
constexpr int kDivision = 480;  // the sample concerto's ticks-per-quarter
}

TEST_CASE("note values convert to ticks", "[ticks]") {
  CHECK(duration_ticks("whole", 0, kDivision) == 1920);
  CHECK(duration_ticks("half", 0, kDivision) == 960);
  CHECK(duration_ticks("quarter", 0, kDivision) == 480);
  CHECK(duration_ticks("eighth", 0, kDivision) == 240);
  CHECK(duration_ticks("16th", 0, kDivision) == 120);
  CHECK(duration_ticks("32nd", 0, kDivision) == 60);
  CHECK(duration_ticks("64th", 0, kDivision) == 30);
  CHECK(duration_ticks("128th", 0, kDivision) == 15);
  CHECK(duration_ticks("breve", 0, kDivision) == 3840);
}

TEST_CASE("dots add half of what precedes them", "[ticks]") {
  CHECK(duration_ticks("half", 1, kDivision) == 1440);     // 960 + 480
  CHECK(duration_ticks("quarter", 1, kDivision) == 720);   // 480 + 240
  CHECK(duration_ticks("quarter", 2, kDivision) == 840);   // 480 + 240 + 120
  CHECK(duration_ticks("quarter", 3, kDivision) == 900);   // + 60
  CHECK(duration_ticks("eighth", 1, kDivision) == 360);
}

TEST_CASE("an unknown duration type is reported rather than guessed", "[ticks]") {
  // "measure" depends on the enclosing time signature, so it cannot be resolved here.
  CHECK_FALSE(duration_ticks("measure", 0, kDivision).has_value());
  CHECK_FALSE(duration_ticks("", 0, kDivision).has_value());
  CHECK_FALSE(duration_ticks("crotchet", 0, kDivision).has_value());
}

TEST_CASE("tuplets scale by their normal-to-actual ratio", "[ticks]") {
  // The concerto's triplets are written normalNotes 2 / actualNotes 3.
  CHECK(apply_tuplet(120, 2, 3) == 80);
  CHECK(apply_tuplet(480, 2, 3) == 320);
  CHECK(apply_tuplet(480, 4, 5) == 384);  // quintuplet
  // A degenerate ratio must not divide by zero.
  CHECK(apply_tuplet(480, 0, 0) == 480);
}

TEST_CASE("fractions normalise and convert to ticks", "[ticks]") {
  CHECK(Fraction(4, 4).to_ticks(kDivision) == 1920);
  CHECK(Fraction(5, 4).to_ticks(kDivision) == 2400);
  CHECK(Fraction(6, 8).to_ticks(kDivision) == 1440);
  CHECK(Fraction(9, 8).to_ticks(kDivision) == 2160);
  CHECK(Fraction(2, 4) == Fraction(1, 2));
  CHECK(Fraction(3, -4) == Fraction(-3, 4));
  CHECK(Fraction(1, 0).is_zero());  // malformed input must not crash
}

TEST_CASE("tick counts describe themselves as note values", "[ticks]") {
  CHECK(describe_ticks(480, kDivision) == "quarter");
  CHECK(describe_ticks(720, kDivision) == "dotted quarter");
  CHECK(describe_ticks(840, kDivision) == "double-dotted quarter");
  CHECK(describe_ticks(1920, kDivision) == "whole");
  CHECK(describe_ticks(77, kDivision) == "77 ticks");
}
