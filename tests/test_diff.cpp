#include <catch2/catch_test_macros.hpp>

#include "motif/diff/engine.h"
#include "motif/mscz/parser.h"
#include "score_builder.h"

using motif::diff::compute;
using motif::mscz::parse_mscx;
using motif::testing::build_score;
using motif::testing::scale_run;
using motif::testing::TestMeasure;
using motif::testing::TestPart;
using motif::testing::transposed;

namespace {

motif::score::Score parse(const std::string& xml) {
  std::string error;
  auto score = parse_mscx(xml, &error);
  INFO("parse error: " << error);
  REQUIRE(score.has_value());
  return *score;
}

/// A four-bar violin part: a run, two rests, then the run again.
std::vector<TestMeasure> baseline_measures() {
  return {
      TestMeasure{.eid = "M1", .notes = scale_run(60, 14)},
      TestMeasure{.eid = "M2", .notes = {}},
      TestMeasure{.eid = "M3", .notes = scale_run(67, 15)},
      TestMeasure{.eid = "M4", .notes = {}},
  };
}

TestPart violin(std::vector<TestMeasure> measures) {
  return TestPart{"Violins, Violin I", "strings.violin", std::move(measures)};
}

TestPart oboe(std::size_t measure_count) {
  std::vector<TestMeasure> measures;
  for (std::size_t i = 0; i < measure_count; ++i) {
    measures.push_back(TestMeasure{.eid = fmt::format("O{}", i + 1), .notes = {}});
  }
  return TestPart{"Oboes, Oboe", "wind.reed.oboe", std::move(measures)};
}

}  // namespace

TEST_CASE("an unchanged score produces an empty diff", "[diff]") {
  const auto score = parse(build_score({violin(baseline_measures())}));
  const auto d = compute(score, score);

  CHECK(d.staves.empty());
  CHECK(d.orchestration.empty());
  CHECK(d.stats.notes_changed == 0);
  CHECK(d.empty());
}

TEST_CASE("changed pitches are counted and located", "[diff]") {
  auto after_measures = baseline_measures();
  after_measures[0].notes = transposed(after_measures[0].notes, 2);

  const auto before = parse(build_score({violin(baseline_measures())}));
  const auto after = parse(build_score({violin(after_measures)}));
  const auto d = compute(before, after);

  REQUIRE(d.staves.size() == 1);
  CHECK(d.staves[0].part_name == "Violin I");
  CHECK(d.staves[0].first_measure == 1);
  CHECK(d.staves[0].last_measure == 1);
  CHECK(d.staves[0].notes_changed == 4);
  CHECK(d.stats.notes_changed == 4);
  CHECK(d.stats.notes_added == 0);
  CHECK(d.stats.notes_removed == 0);
}

TEST_CASE("an inserted measure does not disturb the bars after it", "[diff]") {
  // The reason motif matches measures by eid rather than by position: without identity,
  // inserting a bar would make every later bar look rewritten.
  auto after_measures = baseline_measures();
  after_measures.insert(after_measures.begin() + 2,
                        TestMeasure{.eid = "NEW", .notes = scale_run(72, 14)});

  const auto before = parse(build_score({violin(baseline_measures())}));
  const auto after = parse(build_score({violin(after_measures)}));
  const auto d = compute(before, after);

  CHECK(d.stats.measures_added == 1);
  CHECK(d.stats.measures_removed == 0);

  // Exactly one run of change: the new bar. The untouched bars around it stay silent.
  REQUIRE(d.staves.size() == 1);
  CHECK(d.staves[0].measures_added == 1);
  CHECK(d.staves[0].notes_changed == 0);
  CHECK(d.staves[0].notes_added == 4);
}

TEST_CASE("a deleted measure is reported once, not once per instrument", "[diff]") {
  // Barlines are score-wide, so removing a bar is a single event even in a large score.
  auto violin_after = baseline_measures();
  violin_after.erase(violin_after.begin() + 1);
  auto oboe_after = oboe(4).measures;
  oboe_after.erase(oboe_after.begin() + 1);

  const auto before = parse(build_score({violin(baseline_measures()), oboe(4)}));
  const auto after = parse(build_score(
      {violin(violin_after), TestPart{"Oboes, Oboe", "wind.reed.oboe", oboe_after}}));
  const auto d = compute(before, after);

  CHECK(d.stats.measures_removed == 1);
  CHECK(d.stats.measures_added == 0);
}

TEST_CASE("a removed instrument is reported as orchestration", "[diff]") {
  const auto before = parse(build_score({violin(baseline_measures()), oboe(4)}));
  const auto after = parse(build_score({violin(baseline_measures())}));
  const auto d = compute(before, after);

  REQUIRE(d.orchestration.changes.size() == 1);
  CHECK(d.orchestration.changes[0].kind == motif::diff::PartChange::Kind::Removed);
  CHECK(d.orchestration.changes[0].name == "Oboe");
  CHECK(d.stats.instruments_removed == 1);
  CHECK(d.stats.instruments_added == 0);

  // The surviving instrument is untouched, so no note-level change is reported.
  CHECK(d.staves.empty());
}

TEST_CASE("an added instrument is reported as orchestration", "[diff]") {
  const auto before = parse(build_score({violin(baseline_measures())}));
  const auto after = parse(build_score({violin(baseline_measures()), oboe(4)}));
  const auto d = compute(before, after);

  REQUIRE(d.orchestration.changes.size() == 1);
  CHECK(d.orchestration.changes[0].kind == motif::diff::PartChange::Kind::Added);
  CHECK(d.orchestration.changes[0].name == "Oboe");
  CHECK(d.stats.instruments_added == 1);
}

TEST_CASE("swapping one instrument for another is an add and a remove", "[diff]") {
  const auto before = parse(build_score({violin(baseline_measures()), oboe(4)}));

  std::vector<TestMeasure> bassoon_measures;
  for (int i = 0; i < 4; ++i) {
    bassoon_measures.push_back(TestMeasure{.eid = fmt::format("B{}", i + 1)});
  }
  const auto after = parse(build_score(
      {violin(baseline_measures()),
       TestPart{"Bassoons, Bassoon", "wind.reed.bassoon", bassoon_measures}}));

  const auto d = compute(before, after);
  REQUIRE(d.orchestration.changes.size() == 2);
  CHECK(d.stats.instruments_removed == 1);
  CHECK(d.stats.instruments_added == 1);
}

TEST_CASE("renaming a part keeps it matched to its instrument", "[diff]") {
  const auto before = parse(build_score({violin(baseline_measures())}));
  const auto after = parse(build_score(
      {TestPart{"Violins, Solo Violin", "strings.violin", baseline_measures()}}));
  const auto d = compute(before, after);

  REQUIRE(d.orchestration.changes.size() == 1);
  CHECK(d.orchestration.changes[0].kind == motif::diff::PartChange::Kind::Renamed);
  CHECK(d.orchestration.changes[0].old_name == "Violin I");
  CHECK(d.orchestration.changes[0].name == "Solo Violin");
  CHECK(d.stats.instruments_renamed == 1);
}

TEST_CASE("a key signature change lists the staves it affects", "[diff]") {
  auto before_measures = baseline_measures();
  before_measures[0].key_sig = -1;
  auto after_measures = baseline_measures();
  after_measures[0].key_sig = 1;

  const auto before = parse(build_score({violin(before_measures)}));
  const auto after = parse(build_score({violin(after_measures)}));
  const auto d = compute(before, after);

  REQUIRE(d.key_sigs.size() == 1);
  CHECK(d.key_sigs[0].before == -1);
  CHECK(d.key_sigs[0].after == 1);
  CHECK(d.key_sigs[0].measure == 1);
  CHECK(d.key_sigs[0].staves == std::vector<std::string>{"Violin I"});
}

TEST_CASE("a time signature change is reported", "[diff]") {
  auto before_measures = baseline_measures();
  before_measures[0].time_sig = std::make_pair(4, 4);
  auto after_measures = baseline_measures();
  after_measures[0].time_sig = std::make_pair(3, 4);

  const auto before = parse(build_score({violin(before_measures)}));
  const auto after = parse(build_score({violin(after_measures)}));
  const auto d = compute(before, after);

  REQUIRE(d.time_sigs.size() == 1);
  CHECK(d.time_sigs[0].before.n == 4);
  CHECK(d.time_sigs[0].after.n == 3);
}

TEST_CASE("the instrument filter narrows both the runs and the counts", "[diff]") {
  auto violin_after = baseline_measures();
  violin_after[0].notes = transposed(violin_after[0].notes, 2);

  auto oboe_before = oboe(4);
  auto oboe_after = oboe(4);
  oboe_before.measures[2].notes = scale_run(60, 14);
  oboe_after.measures[2].notes = scale_run(62, 16);

  const auto before = parse(build_score({violin(baseline_measures()), oboe_before}));
  const auto after = parse(build_score({violin(violin_after), oboe_after}));

  const auto all = compute(before, after);
  CHECK(all.staves.size() == 2);
  CHECK(all.stats.notes_changed == 8);

  motif::diff::Options options;
  options.instrument = "violin";
  const auto filtered = compute(before, after, options);
  REQUIRE(filtered.staves.size() == 1);
  CHECK(filtered.staves[0].part_name == "Violin I");
  CHECK(filtered.stats.notes_changed == 4);
}

TEST_CASE("the measure filter narrows both the runs and the counts", "[diff]") {
  auto after_measures = baseline_measures();
  after_measures[0].notes = transposed(after_measures[0].notes, 2);
  after_measures[2].notes = transposed(after_measures[2].notes, 2);

  const auto before = parse(build_score({violin(baseline_measures())}));
  const auto after = parse(build_score({violin(after_measures)}));

  CHECK(compute(before, after).stats.notes_changed == 8);

  motif::diff::Options options;
  options.measures = std::make_pair(3, 4);
  const auto filtered = compute(before, after, options);
  REQUIRE(filtered.staves.size() == 1);
  CHECK(filtered.staves[0].first_measure == 3);
  CHECK(filtered.stats.notes_changed == 4);
}

TEST_CASE("a rewritten bar with fresh element ids still aligns by measure", "[diff]") {
  // MuseScore keeps the measure's eid when its contents are replaced wholesale. The
  // measure must still be recognised, with its notes reported as added and removed.
  auto after_measures = baseline_measures();
  after_measures[0].notes = {{72, 14}, {74, 16}};
  after_measures[0].element_prefix = "rewritten";

  const auto before = parse(build_score({violin(baseline_measures())}));
  const auto after = parse(build_score({violin(after_measures)}));
  const auto d = compute(before, after);

  REQUIRE(d.staves.size() == 1);
  CHECK(d.staves[0].first_measure == 1);
  CHECK(d.stats.measures_added == 0);
  CHECK(d.stats.measures_removed == 0);
  CHECK(d.staves[0].total_changes() > 0);
}
