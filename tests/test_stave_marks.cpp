#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>

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

TEST_CASE("unified, a changed bar appears as it was and then as it is", "[stave-marks][unified]") {
  auto edited = baseline();
  edited[2].notes[1] = TestNote{70, 12};  // A4 becomes Bb4, on beat 2 of bar 3

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto u = motif::render::unified_diff(compute(before, after), before, after, "HEAD", "now");

  INFO(u.json);
  CHECK(u.hunks == 1);
  CHECK(u.dropped == 0);
  // Bars 1 and 2 once, then bar 3 as it was (the score's bar 3) and as it is (bar 4).
  CHECK(contains(u.json, R"({"tone": "removed", "bar": 3, "staff": 1, "voice": 1, "at": "1", "pitch": 69})"));
  CHECK(contains(u.json, R"({"tone": "added", "bar": 4, "staff": 1, "voice": 1, "at": "1", "pitch": 70})"));
  CHECK(u.marks == 4);  // the two notes, and the two bars' backgrounds
  CHECK(contains(u.json, R"({"tone": "removed", "bar": 3, "fill": true})"));
  CHECK(contains(u.json, R"({"tone": "added", "bar": 4, "fill": true})"));
  REQUIRE(u.start_bar.has_value());
  CHECK(*u.start_bar == 2);  // a bar before the change

  pugi::xml_document doc;
  REQUIRE(doc.load_string(u.mei.c_str()));
  const auto measures = doc.select_nodes("//section/measure");
  REQUIRE(measures.size() == 4);
  CHECK(std::string(measures[2].node().attribute("label").as_string()) == "3");
  CHECK(std::string(measures[3].node().attribute("label").as_string()) == "3");
  CHECK(std::string(measures[3].node().attribute("n").as_string()) == "4");
  CHECK(std::string(measures[2].node().attribute("right").as_string()) == "dashed");
  CHECK(std::string(measures[2].node().child("dir").text().as_string()) == "HEAD");
  CHECK(std::string(measures[3].node().child("dir").text().as_string()) == "now");
  CHECK_FALSE(measures[1].node().child("dir"));
}

TEST_CASE("unified, deleted bars are red and inserted ones green", "[stave-marks][unified]") {
  auto edited = baseline();
  edited.erase(edited.begin() + 1);  // bar 2 deleted
  edited.push_back(TestMeasure{.eid = "NEW", .notes = scale_run(64, 18), .element_prefix = "new"});

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited)}));
  const auto u = motif::render::unified_diff(compute(before, after), before, after, "HEAD", "now");

  INFO(u.json);
  // Bar 1, the deleted bar 2, old bar 3 (now bar 2), then the inserted bar.
  CHECK(u.hunks == 2);
  CHECK(contains(u.json, R"({"tone": "removed", "bar": 2})"));
  CHECK(contains(u.json, R"({"tone": "added", "bar": 4})"));
}

TEST_CASE("unified, an added instrument is green wherever the new version shows", "[stave-marks][unified]") {
  auto edited = baseline();
  edited[0].notes[0] = TestNote{62, 16};  // and bar 1 of the violin changes

  const auto before = parse(build_score({violin(baseline())}));
  const auto after = parse(build_score({violin(edited), cello(baseline())}));
  const auto u = motif::render::unified_diff(compute(before, after), before, after, "HEAD", "now");

  INFO(u.json);
  // Old bar 1 (no cello), new bar 1, bars 2 and 3: the cello is green in output bars 2 to 4.
  CHECK(contains(u.json, R"({"tone": "added", "bar": 2, "to": 4, "staff": 2})"));

  pugi::xml_document doc;
  REQUIRE(doc.load_string(u.mei.c_str()));
  CHECK(doc.select_node("//section/measure[1]/staff[@n='2']/layer/mSpace"));
}

TEST_CASE("unified, unchanged bars are left out but for some context, as git diff does",
          "[stave-marks][unified]") {
  std::vector<TestMeasure> measures;
  for (int m = 0; m < 10; ++m) {
    measures.push_back(TestMeasure{.eid = fmt::format("M{}", m + 1), .notes = scale_run(60, 14)});
  }
  auto edited = measures;
  edited[5].notes[0] = TestNote{62, 16};  // bar 6

  const auto before = parse(build_score({violin(measures)}));
  const auto after = parse(build_score({violin(edited)}));
  const auto u = motif::render::unified_diff(compute(before, after), before, after, "HEAD", "now", 1);

  INFO(u.json);
  // Bar 5 for context, bar 6 as it was and as it is, then bar 7.
  pugi::xml_document doc;
  REQUIRE(doc.load_string(u.mei.c_str()));
  const auto shown = doc.select_nodes("//section/measure");
  REQUIRE(shown.size() == 4);
  const auto label = [&](std::size_t i) {
    return std::string(shown[i].node().attribute("label").as_string());
  };
  CHECK(label(0) == "5");
  CHECK(label(1) == "6");
  CHECK(label(2) == "6");
  CHECK(label(3) == "7");
  CHECK(std::string(shown[0].node().child("dir").text().as_string()) ==
        "\xe2\x80\xa6 4 bars unchanged");  // bars 1-4 left out before it
  CHECK(std::string(shown[3].node().attribute("n").as_string()) == "4");

  CHECK(contains(u.json, R"({"tone": "removed", "bar": 2, "staff": 1, "voice": 1, "at": "0", "pitch": 60})"));
  CHECK(contains(u.json, R"({"tone": "added", "bar": 3, "staff": 1, "voice": 1, "at": "0", "pitch": 62})"));
  REQUIRE(u.start_bar.has_value());
  CHECK(*u.start_bar == 1);

  // With every bar shown, nothing is left out.
  const auto all = motif::render::unified_diff(compute(before, after), before, after, "HEAD", "now");
  pugi::xml_document whole;
  REQUIRE(whole.load_string(all.mei.c_str()));
  CHECK(whole.select_nodes("//section/measure").size() == 11);
}
