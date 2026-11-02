#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "motif/analysis/transform.h"
#include "motif/diff/engine.h"
#include "motif/mscz/parser.h"
#include "score_builder.h"

using motif::analysis::detect;
using motif::diff::Transform;
using motif::score::Note;
using motif::score::Pitch;

namespace {

/// Builds a note sequence from MIDI numbers, spelled naturally along the fifths.
std::vector<Note> notes(std::initializer_list<int> midi) {
  std::vector<Note> out;
  for (const int m : midi) {
    Note note;
    note.pitch = Pitch{m, 14};
    out.push_back(note);
  }
  return out;
}

std::vector<int> ticks(std::initializer_list<int> values) { return {values}; }

/// A uniform rhythm of `n` quarter notes.
std::vector<int> quarters(std::size_t n) { return std::vector<int>(n, 480); }

}  // namespace

TEST_CASE("a phrase moved by one interval is reported as a transposition", "[transform]") {
  const auto before = notes({62, 65, 69, 67});
  const auto after = notes({64, 67, 71, 69});

  const auto found = detect(before, quarters(4), after, quarters(4));
  REQUIRE(found.size() == 1);
  CHECK(found[0].kind == Transform::Kind::Transposition);
  CHECK(found[0].semitones == 2);
  CHECK(found[0].note_count == 4);
}

TEST_CASE("downward transposition carries a negative interval", "[transform]") {
  const auto found = detect(notes({67, 69, 71}), quarters(3), notes({64, 66, 68}), quarters(3));
  REQUIRE(found.size() == 1);
  CHECK(found[0].semitones == -3);
}

TEST_CASE("one wrong note suppresses the transposition entirely", "[transform]") {
  // The central promise of the design: a near-miss reports nothing rather than
  // something approximately true.
  const auto before = notes({62, 65, 69, 67});
  auto after = notes({64, 67, 71, 69});
  after[2].pitch.midi = 72;  // this one note did not move by 2

  CHECK(detect(before, quarters(4), after, quarters(4)).empty());
}

TEST_CASE("a transposition with an altered rhythm is not reported", "[transform]") {
  // Every pitch moved by 2, but the third note's duration changed, so this is not
  // simply a transposition of the phrase.
  const auto before = notes({62, 65, 69, 67});
  const auto after = notes({64, 67, 71, 69});

  CHECK(detect(before, ticks({480, 480, 480, 480}), after, ticks({480, 480, 240, 480})).empty());
}

TEST_CASE("phrases shorter than three notes are never reported", "[transform]") {
  // Two notes a step apart are a coincidence, not a transformation.
  CHECK(detect(notes({62, 65}), quarters(2), notes({64, 67}), quarters(2)).empty());
  CHECK(detect(notes({62}), quarters(1), notes({64}), quarters(1)).empty());
}

TEST_CASE("an unchanged phrase is not a transposition", "[transform]") {
  const auto same = notes({62, 65, 69});
  CHECK(detect(same, quarters(3), same, quarters(3)).empty());
}

TEST_CASE("sequences of different lengths are not compared", "[transform]") {
  CHECK(detect(notes({62, 65, 69}), quarters(3), notes({64, 67, 71, 74}), quarters(4)).empty());
}

TEST_CASE("uniformly lengthened durations are an augmentation", "[transform]") {
  const auto same = notes({62, 65, 69, 67});
  const auto found = detect(same, ticks({240, 240, 480, 240}), same, ticks({480, 480, 960, 480}));

  REQUIRE(found.size() == 1);
  CHECK(found[0].kind == Transform::Kind::Augmentation);
  CHECK(found[0].ratio_num == 2);
  CHECK(found[0].ratio_den == 1);
}

TEST_CASE("uniformly shortened durations are a diminution", "[transform]") {
  const auto same = notes({62, 65, 69});
  const auto found = detect(same, ticks({480, 480, 960}), same, ticks({240, 240, 480}));

  REQUIRE(found.size() == 1);
  CHECK(found[0].kind == Transform::Kind::Diminution);
  CHECK(found[0].ratio_num == 1);
  CHECK(found[0].ratio_den == 2);
}

TEST_CASE("an inconsistent rhythmic scaling is not reported", "[transform]") {
  // Two notes doubled and one tripled is not an augmentation of the phrase.
  const auto same = notes({62, 65, 69});
  CHECK(detect(same, ticks({240, 240, 240}), same, ticks({480, 480, 720})).empty());
}

TEST_CASE("changing both pitch and rhythm reports nothing", "[transform]") {
  // Real recomposition matches no single relationship, and motif says so by staying
  // silent rather than picking the closest label.
  CHECK(detect(notes({62, 65, 69}), ticks({480, 480, 480}), notes({64, 69, 72}),
               ticks({240, 480, 960}))
            .empty());
}

TEST_CASE("mirrored intervals are an inversion", "[transform]") {
  // Up a third then up a fourth becomes down a third then down a fourth.
  const auto before = notes({60, 64, 69});
  const auto after = notes({60, 56, 51});

  const auto found = detect(before, quarters(3), after, quarters(3));
  REQUIRE(found.size() == 1);
  CHECK(found[0].kind == Transform::Kind::Inversion);
}

TEST_CASE("reversed material is a retrograde", "[transform]") {
  const auto before = notes({60, 64, 67, 72});
  const auto after = notes({72, 67, 64, 60});

  const auto found = detect(before, quarters(4), after, quarters(4));
  REQUIRE(found.size() == 1);
  CHECK(found[0].kind == Transform::Kind::Retrograde);
}

TEST_CASE("a palindrome is not reported as its own retrograde", "[transform]") {
  const auto same = notes({60, 64, 60});
  CHECK(detect(same, quarters(3), same, quarters(3)).empty());
}

TEST_CASE("a transposed run in a real diff is annotated", "[transform][diff]") {
  using motif::testing::build_score;
  using motif::testing::scale_run;
  using motif::testing::TestMeasure;
  using motif::testing::TestPart;
  using motif::testing::transposed;

  const auto parse = [](const std::string& xml) {
    std::string error;
    auto score = motif::mscz::parse_mscx(xml, &error);
    INFO("parse error: " << error);
    REQUIRE(score.has_value());
    return *score;
  };

  std::vector<TestMeasure> before_measures{
      TestMeasure{.eid = "M1", .notes = scale_run(60, 14)},
      TestMeasure{.eid = "M2", .notes = {}},
  };
  auto after_measures = before_measures;
  after_measures[0].notes = transposed(after_measures[0].notes, 2);

  const auto before = parse(build_score({TestPart{"Violins, Violin I", "strings.violin",
                                                  before_measures}}));
  const auto after = parse(build_score({TestPart{"Violins, Violin I", "strings.violin",
                                                 after_measures}}));

  const auto d = motif::diff::compute(before, after);
  REQUIRE(d.staves.size() == 1);
  REQUIRE(d.staves[0].transforms.size() == 1);
  CHECK(d.staves[0].transforms[0].kind == Transform::Kind::Transposition);
  CHECK(d.staves[0].transforms[0].semitones == 2);
}
