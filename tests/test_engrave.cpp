// Engraves motif's MEI with the real stave, built alongside motif. stave's --png draws
// the first screen without needing a terminal, and fails if Verovio can't read the
// score -- or, given marks, if any mark misses its note.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "motif/diff/engine.h"
#include "motif/export/mei.h"
#include "motif/git/subprocess.h"
#include "motif/mscz/parser.h"
#include "motif/render/stave_marks.h"

namespace fs = std::filesystem;

namespace {

/// A folder of its own under the system's temp folder, removed afterwards.
class TempDir {
 public:
  TempDir() {
    std::random_device rd;
    path_ = fs::temp_directory_path() / ("motif-test-" + std::to_string(rd()));
    fs::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

void write_file(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  REQUIRE(out.good());
}

/// Runs stave on `mei`, with `marks` if given; returns its result.
motif::proc::Result run_stave(const TempDir& dir, const std::string& mei,
                              const std::string& marks = "") {
  const auto mei_path = dir.path() / "score.mei";
  write_file(mei_path, mei);
  std::vector<std::string> argv{MOTIF_STAVE_EXE, mei_path.string(), "--png",
                                (dir.path() / "out.png").string()};
  if (!marks.empty()) {
    const auto marks_path = dir.path() / "marks.json";
    write_file(marks_path, marks);
    argv.push_back("--marks");
    argv.push_back(marks_path.string());
  }
  return motif::proc::run(argv);
}

motif::score::Score load(const fs::path& path) {
  std::string error;
  auto score = motif::mscz::parse_score_file(path, &error);
  INFO(error);
  REQUIRE(score.has_value());
  return std::move(*score);
}

}  // namespace

TEST_CASE("stave engraves motif's MEI of each fixture", "[engrave]") {
  for (const char* name : {"v1.mscx", "v2_transposed.mscx", "v3_no_oboe.mscx"}) {
    INFO(name);
    TempDir dir;
    const auto r = run_stave(dir, motif::mei::to_mei(load(fs::path(MOTIF_FIXTURES) / name)));
    INFO(r.err);
    CHECK(r.ok());
  }
}

#ifdef MOTIF_SAMPLE_SCORE
TEST_CASE("stave engraves motif's MEI of the sample concerto", "[engrave][sample]") {
  TempDir dir;
  const auto r = run_stave(dir, motif::mei::to_mei(load(MOTIF_SAMPLE_SCORE)));
  INFO(r.err);
  CHECK(r.ok());
}
#endif

TEST_CASE("stave finds every mark motif makes for the fixture diffs", "[engrave]") {
  const auto v1 = load(fs::path(MOTIF_FIXTURES) / "v1.mscx");
  for (const char* name : {"v2_transposed.mscx", "v3_no_oboe.mscx"}) {
    INFO(name);
    const auto after = load(fs::path(MOTIF_FIXTURES) / name);
    const auto marks = motif::render::stave_marks(motif::diff::compute(v1, after), after);
    INFO(marks.json);
    CHECK(marks.marks > 0);
    CHECK(marks.dropped == 0);

    TempDir dir;
    const auto r = run_stave(dir, motif::mei::to_mei(after), marks.json);
    INFO(r.err);
    CHECK(r.ok());
  }
}

#ifdef MOTIF_SAMPLE_SCORE
TEST_CASE("stave finds every note of the concerto by motif's bar, staff, voice, onset and pitch",
          "[engrave][sample]") {
  // Raising every note a semitone marks every note of the score as changed, so stave
  // must agree with motif on where each one is: through tuplets, voices that start late,
  // irregular bars and transposing instruments.
  const auto before = load(MOTIF_SAMPLE_SCORE);
  auto after = before;
  int raised = 0;
  for (auto& staff : after.staves) {
    for (auto& measure : staff.measures) {
      for (auto& voice : measure.voices) {
        for (auto& el : voice.elements) {
          for (auto& note : el.notes) {
            note.pitch.midi += 1;
            note.pitch.tpc += 7;  // the same letter, a sharp higher
            ++raised;
          }
        }
      }
    }
  }

  const auto marks = motif::render::stave_marks(motif::diff::compute(before, after), after);
  CHECK(marks.dropped == 0);
  CHECK(marks.marks == raised);

  TempDir dir;
  const auto r = run_stave(dir, motif::mei::to_mei(after), marks.json);
  INFO(r.err);
  CHECK(r.ok());
}
#endif

namespace {

/// Runs stave --compare on two versions with a comparison's marks; returns its result.
motif::proc::Result run_compare(const TempDir& dir, const motif::score::Score& before,
                                const motif::score::Score& after,
                                const motif::render::StaveComparison& c) {
  write_file(dir.path() / "before.mei", motif::mei::to_mei(before));
  write_file(dir.path() / "after.mei", motif::mei::to_mei(after));
  write_file(dir.path() / "before.json", c.before_json);
  write_file(dir.path() / "after.json", c.after_json);
  std::vector<std::string> argv{MOTIF_STAVE_EXE,
                                "--compare",
                                (dir.path() / "before.mei").string(),
                                (dir.path() / "after.mei").string(),
                                "--before-marks",
                                (dir.path() / "before.json").string(),
                                "--marks",
                                (dir.path() / "after.json").string(),
                                "--png",
                                (dir.path() / "out.png").string()};
  if (!c.pairs.empty()) argv.insert(argv.end(), {"--pairs", c.pairs});
  return motif::proc::run(argv);
}

}  // namespace

TEST_CASE("stave compares the fixture versions side by side, finding every mark", "[engrave]") {
  const auto v1 = load(fs::path(MOTIF_FIXTURES) / "v1.mscx");
  for (const char* name : {"v2_transposed.mscx", "v3_no_oboe.mscx"}) {
    INFO(name);
    const auto after = load(fs::path(MOTIF_FIXTURES) / name);
    const auto c = motif::render::stave_comparison(motif::diff::compute(v1, after), v1, after);
    INFO(c.before_json << c.after_json);
    CHECK(c.marks > 0);
    CHECK(c.dropped == 0);

    TempDir dir;
    const auto r = run_compare(dir, v1, after, c);
    INFO(r.err);
    CHECK(r.ok());
  }
}

#ifdef MOTIF_SAMPLE_SCORE
TEST_CASE("stave compares the concerto with every note raised, finding every note on both sides",
          "[engrave][sample]") {
  const auto before = load(MOTIF_SAMPLE_SCORE);
  auto after = before;
  int raised = 0;
  for (auto& staff : after.staves) {
    for (auto& measure : staff.measures) {
      for (auto& voice : measure.voices) {
        for (auto& el : voice.elements) {
          for (auto& note : el.notes) {
            note.pitch.midi += 1;
            note.pitch.tpc += 7;
            ++raised;
          }
        }
      }
    }
  }

  const auto c = motif::render::stave_comparison(motif::diff::compute(before, after), before, after);
  CHECK(c.dropped == 0);
  CHECK(c.marks == 2 * raised);

  TempDir dir;
  const auto r = run_compare(dir, before, after, c);
  INFO(r.err);
  CHECK(r.ok());
}
#endif

TEST_CASE("stave shows the unified fixture diffs, finding every mark", "[engrave]") {
  const auto v1 = load(fs::path(MOTIF_FIXTURES) / "v1.mscx");
  for (const char* name : {"v2_transposed.mscx", "v3_no_oboe.mscx"}) {
    INFO(name);
    const auto after = load(fs::path(MOTIF_FIXTURES) / name);
    const auto u = motif::render::unified_diff(motif::diff::compute(v1, after), v1, after, "HEAD",
                                               "working tree");
    INFO(u.json);
    CHECK(u.dropped == 0);
    if (u.marks == 0) continue;  // only an instrument removed: nothing to draw

    TempDir dir;
    const auto r = run_stave(dir, u.mei, u.json);
    INFO(r.err);
    CHECK(r.ok());
  }
}

#ifdef MOTIF_SAMPLE_SCORE
TEST_CASE("stave shows the concerto with every note raised as one unified score", "[engrave][sample]") {
  const auto before = load(MOTIF_SAMPLE_SCORE);
  auto after = before;
  int raised = 0;
  for (auto& staff : after.staves) {
    for (auto& measure : staff.measures) {
      for (auto& voice : measure.voices) {
        for (auto& el : voice.elements) {
          for (auto& note : el.notes) {
            note.pitch.midi += 1;
            note.pitch.tpc += 7;
            ++raised;
          }
        }
      }
    }
  }

  const auto u = motif::render::unified_diff(motif::diff::compute(before, after), before, after,
                                             "HEAD", "working tree");
  CHECK(u.dropped == 0);
  CHECK(u.marks == 2 * raised);

  TempDir dir;
  const auto r = run_stave(dir, u.mei, u.json);
  INFO(r.err);
  CHECK(r.ok());
}
#endif
