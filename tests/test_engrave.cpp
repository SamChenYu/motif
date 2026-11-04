// Engraves motif's MEI with the real stave, built alongside motif. stave's --png draws
// the first screen without needing a terminal, and fails if Verovio can't read the
// score -- or, given marks, if any mark misses its note.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "motif/export/mei.h"
#include "motif/git/subprocess.h"
#include "motif/mscz/parser.h"

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
