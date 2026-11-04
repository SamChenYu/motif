#include <catch2/catch_test_macros.hpp>

#include <string>

#include "motif/diff/engine.h"
#include "motif/mscz/parser.h"
#include "motif/render/stave_marks.h"
#include "score_builder.h"

using motif::diff::compute;
using motif::render::stave_marks;
using motif::testing::build_score;
using motif::testing::scale_run;
using motif::testing::TestMeasure;
using motif::testing::TestNote;
using motif::testing::TestPart;
using motif::testing::transposed;

namespace {

motif::score::Score parse(const std::string& xml) {
  std::string error;
  auto score = motif::mscz::parse_mscx(xml, &error);
  INFO("parse error: " << error);
  REQUIRE(score.has_value());
  return *score;
}

std::vector<TestMeasure> baseline() {
  return {
      TestMeasure{.eid = "M1", .notes = scale_run(60, 14)},
      TestMeasure{.eid = "M2", .notes = {}},
      TestMeasure{.eid = "M3", .notes = scale_run(67, 15)},
  };
}

TestPart violin(std::vector<TestMeasure> measures) {
  return TestPart{"Violins, Violin I", "strings.violin", std::move(measures)};
}

TestPart cello(std::vector<TestMeasure> measures) {
  return TestPart{"Violoncellos, Violoncello", "strings.cello", std::move(measures)};
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("changed notes are marked one by one, by bar, staff, voice, time and pitch",
          "[stave-marks]") {
  auto edited = baseline();
  edited[2].notes[1] = TestNote{70, 12};  // A4 becomes Bb4, on beat 2 of bar 3

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto marks = stave_marks(compute(before, after), after);

  INFO(marks.json);
  CHECK(marks.marks == 1);
  CHECK(marks.dropped == 0);
  CHECK(contains(marks.json,
                 R"({"tone": "changed", "bar": 3, "staff": 1, "voice": 1, "at": "1", "pitch": 70})"));
  REQUIRE(marks.start_bar.has_value());
  CHECK(*marks.start_bar == 3);
  CHECK(marks.start_staff == 1);
}

TEST_CASE("added notes are marked as added", "[stave-marks]") {
  auto edited = baseline();
  edited[1].notes = scale_run(72, 14);  // the empty bar 2 gains a run

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto marks = stave_marks(compute(before, after), after);

  INFO(marks.json);
  CHECK(marks.marks == 4);
  CHECK(contains(marks.json, R"({"tone": "added", "bar": 2, "staff": 1, "voice": 1, "at": "0", "pitch": 72})"));
  CHECK(contains(marks.json, R"("at": "3", "pitch": 77})"));
}

TEST_CASE("a bar that only lost notes is marked removed, on its staff", "[stave-marks]") {
  auto edited = baseline();
  edited[0].notes.pop_back();  // bar 1 loses its last note

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto marks = stave_marks(compute(before, after), after);

  INFO(marks.json);
  CHECK(marks.marks == 1);
  CHECK(contains(marks.json, R"({"tone": "removed", "bar": 1, "staff": 1})"));
}

TEST_CASE("a bar with other marks is not also marked removed", "[stave-marks]") {
  // stave can't colour a bar red and a note in it yellow, so the note wins.
  auto edited = baseline();
  edited[0].notes.pop_back();
  edited[0].notes[0] = TestNote{61, 21};

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto marks = stave_marks(compute(before, after), after);

  INFO(marks.json);
  CHECK(marks.marks == 1);
  CHECK(contains(marks.json, R"("tone": "changed", "bar": 1)"));
  CHECK_FALSE(contains(marks.json, "removed"));
}

TEST_CASE("an added instrument is marked all the way through its staff", "[stave-marks]") {
  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(baseline()), cello(baseline())}));
  const auto marks = stave_marks(compute(before, after), after);

  INFO(marks.json);
  CHECK(marks.marks == 1);
  CHECK(contains(marks.json, R"({"tone": "added", "bar": 1, "to": 3, "staff": 2})"));
  REQUIRE(marks.start_bar.has_value());
  CHECK(marks.start_staff == 2);
}

TEST_CASE("an inserted bar is marked on every staff, and its notes are not marked again",
          "[stave-marks]") {
  auto edited = baseline();
  edited.insert(edited.begin() + 1, TestMeasure{.eid = "NEW", .notes = scale_run(64, 18),
                                                .element_prefix = "new"});

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto marks = stave_marks(compute(before, after), after);

  INFO(marks.json);
  CHECK(marks.marks == 1);
  CHECK(contains(marks.json, R"({"tone": "added", "bar": 2})"));
}

TEST_CASE("a transposed run is one mark per note", "[stave-marks]") {
  auto edited = baseline();
  edited[0].notes = transposed(edited[0].notes, 2);

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto marks = stave_marks(compute(before, after), after);

  INFO(marks.json);
  CHECK(marks.marks == 4);
  CHECK(marks.dropped == 0);
}

TEST_CASE("an unchanged score has no marks and nowhere to start", "[stave-marks]") {
  const auto score = parse(build_score({violin(baseline())}));
  const auto marks = stave_marks(compute(score, score), score);

  CHECK(marks.marks == 0);
  CHECK(marks.json == "[\n]\n");
  CHECK_FALSE(marks.start_bar.has_value());
}

TEST_CASE("side by side, a changed note is marked as it was and as it is", "[stave-marks]") {
  auto edited = baseline();
  edited[2].notes[1] = TestNote{70, 12};  // A4 becomes Bb4, on beat 2 of bar 3

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto c = motif::render::stave_comparison(compute(before, after), before, after);

  INFO(c.before_json << c.after_json);
  CHECK(contains(c.before_json, R"({"tone": "changed", "bar": 3, "staff": 1, "voice": 1, "at": "1", "pitch": 69})"));
  CHECK(contains(c.after_json, R"({"tone": "changed", "bar": 3, "staff": 1, "voice": 1, "at": "1", "pitch": 70})"));
  CHECK(c.marks == 2);
  CHECK(c.dropped == 0);
  CHECK(c.pairs == "1-3=1-3");
  REQUIRE(c.start_bar.has_value());
  CHECK(*c.start_bar == 3);
}

TEST_CASE("side by side, a removed note is red where it was", "[stave-marks]") {
  auto edited = baseline();
  edited[0].notes.pop_back();  // bar 1 loses its last note, on beat 4

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto c = motif::render::stave_comparison(compute(before, after), before, after);

  INFO(c.before_json << c.after_json);
  CHECK(contains(c.before_json, R"({"tone": "removed", "bar": 1, "staff": 1, "voice": 1, "at": "3", "pitch": 65})"));
  CHECK(c.after_json == "[\n]\n");
  CHECK(c.marks == 1);
}

TEST_CASE("side by side, inserted and deleted bars are marked and paired around", "[stave-marks]") {
  auto edited = baseline();
  edited.erase(edited.begin() + 1);  // bar 2 deleted...
  edited.push_back(TestMeasure{.eid = "NEW", .notes = scale_run(64, 18), .element_prefix = "new"});

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto c = motif::render::stave_comparison(compute(before, after), before, after);

  INFO(c.before_json << c.after_json << c.pairs);
  CHECK(contains(c.before_json, R"({"tone": "removed", "bar": 2})"));
  CHECK(contains(c.after_json, R"({"tone": "added", "bar": 3})"));
  CHECK(c.pairs == "1=1,3=2");
}

TEST_CASE("side by side, a removed instrument is red all the way through", "[stave-marks]") {
  const auto before = parse(build_score({violin(baseline()), cello(baseline())}));
  const auto after = parse(build_score({violin(baseline())}));
  const auto c = motif::render::stave_comparison(compute(before, after), before, after);

  INFO(c.before_json);
  CHECK(contains(c.before_json, R"({"tone": "removed", "bar": 1, "to": 3, "staff": 2})"));
  CHECK(c.after_json == "[\n]\n");
}
