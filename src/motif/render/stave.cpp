#include "motif/render/stave.h"

#include <fmt/format.h>

#include <cstdlib>
#include <fstream>
#include <random>
#include <string_view>
#include <vector>

#include "motif/git/subprocess.h"

namespace motif::render {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr std::string_view kStaveName = "stave.exe";
constexpr char kPathSeparator = ';';
#else
constexpr std::string_view kStaveName = "stave";
constexpr char kPathSeparator = ':';
#endif

const char* env_or_null(const char* name) {
  const char* value = std::getenv(name);
  return (value != nullptr && value[0] != '\0') ? value : nullptr;
}

bool is_file(const fs::path& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

/// A folder of its own for stave's input, removed when done.
class ScratchDir {
 public:
  ScratchDir() {
    std::random_device rd;
    std::error_code ec;
    path_ = fs::temp_directory_path(ec) / fmt::format("motif-{:08x}", rd());
    fs::create_directories(path_, ec);
    if (ec) path_.clear();
  }
  ~ScratchDir() {
    if (path_.empty()) return;
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  [[nodiscard]] const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

bool write_file(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  return out.good();
}

}  // namespace

std::optional<fs::path> find_stave() {
  if (const char* forced = env_or_null("MOTIF_STAVE")) {
    if (is_file(forced)) return fs::path(forced);
    return std::nullopt;
  }

  if (const auto self = proc::self_executable(); !self.empty()) {
    const auto beside = self.parent_path() / kStaveName;
    if (is_file(beside)) return beside;
  }

  if (const char* path = env_or_null("PATH")) {
    std::string_view rest = path;
    while (!rest.empty()) {
      const auto end = rest.find(kPathSeparator);
      const auto dir = rest.substr(0, end);
      if (!dir.empty()) {
        const auto candidate = fs::path(std::string(dir)) / kStaveName;
        if (is_file(candidate)) return candidate;
      }
      if (end == std::string_view::npos) break;
      rest.remove_prefix(end + 1);
    }
  }
  return std::nullopt;
}

bool built_with_stave() {
#ifdef MOTIF_HAVE_STAVE
  return true;
#else
  return false;
#endif
}

int show_in_stave(const fs::path& stave, const std::string& mei, const StaveMarks& marks,
                  const std::string& name) {
  ScratchDir scratch;
  fs::path dir = scratch.path();
  if (const char* dump = env_or_null("MOTIF_DUMP_NOTATION")) {
    dir = dump;
    std::error_code ec;
    fs::create_directories(dir, ec);
  }
  if (dir.empty()) {
    fmt::print(stderr, "motif: can't make a temporary folder for stave\n");
    return 1;
  }

  const auto score_path = dir / "score.mei";
  const auto marks_path = dir / "marks.json";
  if (!write_file(score_path, mei) || !write_file(marks_path, marks.json)) {
    fmt::print(stderr, "motif: can't write stave's files to {}\n", dir.string());
    return 1;
  }

  std::vector<std::string> argv{stave.string(), score_path.string(), "--marks",
                                marks_path.string(), "--name", name};
  if (marks.start_bar) {
    argv.insert(argv.end(), {"--bar", std::to_string(*marks.start_bar), "--staff",
                             std::to_string(marks.start_staff)});
  }

  const auto r = proc::run_attached(argv);
  if (!r.err.empty()) {
    fmt::print(stderr, "{}\n", r.err);
    return 1;
  }
  return r.exit_code;
}

}  // namespace motif::render
