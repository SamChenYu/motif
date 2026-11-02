#include "motif/cli/score_source.h"

#include <fmt/format.h>

#include <algorithm>
#include <fstream>

#include "motif/mscz/parser.h"

namespace motif::cli {
namespace {

std::optional<std::string> read_working_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Blob-id-keyed cache of parsed scores.
///
/// Small and fixed: `log` only ever needs the current commit's score and its parent's,
/// so a handful of entries captures every reuse without holding a whole history's worth
/// of parsed scores in memory.
class ScoreCache {
 public:
  static constexpr std::size_t kCapacity = 4;

  [[nodiscard]] ScorePtr get(const std::string& oid) const {
    for (const auto& entry : entries_) {
      if (entry.first == oid) return entry.second;
    }
    return nullptr;
  }

  void put(std::string oid, ScorePtr score) {
    if (entries_.size() >= kCapacity) entries_.erase(entries_.begin());
    entries_.emplace_back(std::move(oid), std::move(score));
  }

 private:
  std::vector<std::pair<std::string, ScorePtr>> entries_;
};

ScoreCache& cache() {
  static ScoreCache instance;
  return instance;
}

}  // namespace

bool is_score_path(const fs::path& path) {
  const auto ext = path.extension().string();
  return ext == ".mscz" || ext == ".mscx";
}

ScorePtr load_score_by_oid(const git::Repo& repo, const std::string& oid, std::string& error) {
  // An all-zero id is git's way of saying the path is absent on one side.
  if (oid.empty() || oid.find_first_not_of('0') == std::string::npos) return nullptr;

  if (auto cached = cache().get(oid)) return cached;

  const auto bytes = repo.read_blob(oid);
  if (!bytes) return nullptr;

  auto parsed = mscz::parse_score(*bytes, &error);
  if (!parsed) return nullptr;

  auto score = std::make_shared<const score::Score>(std::move(*parsed));
  cache().put(oid, score);
  return score;
}

ScorePtr load_score(const git::Repo& repo, const Revision& revision, const fs::path& rel,
                    std::string& error) {
  // Working-tree content has no stable identity, so it is read fresh every time.
  if (!revision.rev) {
    const auto bytes = read_working_file(repo.root() / rel);
    if (!bytes) return nullptr;
    auto parsed = mscz::parse_score(*bytes, &error);
    return parsed ? std::make_shared<const score::Score>(std::move(*parsed)) : nullptr;
  }

  const auto oid = repo.blob_oid_at(*revision.rev, rel);
  if (!oid) return nullptr;
  return load_score_by_oid(repo, *oid, error);
}

std::vector<fs::path> changed_scores(const git::Repo& repo, const Revision& from,
                                     const Revision& to) {
  // `git diff <from>` with no second revision already compares against the working
  // tree, which is exactly what an omitted `to` means here.
  const std::string from_rev = from.rev.value_or("HEAD");
  const std::string to_rev = to.rev.value_or("");

  std::vector<fs::path> scores;
  for (auto& path : repo.changed_paths(from_rev, to_rev)) {
    if (is_score_path(path)) scores.push_back(std::move(path));
  }

  std::sort(scores.begin(), scores.end());
  scores.erase(std::unique(scores.begin(), scores.end()), scores.end());
  return scores;
}

std::vector<fs::path> all_tracked_scores(const git::Repo& repo) {
  const auto result = repo.git({"ls-files", "-z"});
  if (!result.ok()) return {};

  std::vector<fs::path> scores;
  std::size_t start = 0;
  while (start < result.out.size()) {
    const auto end = result.out.find('\0', start);
    const auto piece = result.out.substr(start, end - start);
    if (!piece.empty()) {
      fs::path path(piece);
      if (is_score_path(path)) scores.push_back(std::move(path));
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }

  std::sort(scores.begin(), scores.end());
  return scores;
}

}  // namespace motif::cli
