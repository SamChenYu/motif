#include "motif/git/repo.h"

#include <fmt/format.h>

#include <charconv>
#include <cstdlib>

namespace motif::git {
namespace {

/// ASCII unit/record separators, used to frame `git log --format` output.
///
/// Commit subjects can contain almost anything including newlines, so field and record
/// boundaries have to be characters git will never emit from metadata.
constexpr char kUnitSep = '\x1f';
constexpr char kRecordSep = '\x1e';

std::string_view trim(std::string_view s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string_view::npos) return {};
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::vector<std::string_view> split(std::string_view s, char delim) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto pos = s.find(delim, start);
    if (pos == std::string_view::npos) {
      parts.push_back(s.substr(start));
      break;
    }
    parts.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

/// Returns everything past the nth space.
///
/// Porcelain v2 entries end with a path that may itself contain spaces, so the fixed
/// leading fields are skipped by counting separators rather than tokenising the line.
std::string_view after_nth_space(std::string_view s, int n) {
  std::size_t pos = 0;
  for (int i = 0; i < n; ++i) {
    pos = s.find(' ', pos);
    if (pos == std::string_view::npos) return {};
    ++pos;
  }
  return s.substr(pos);
}

int parse_int(std::string_view s, int fallback = 0) {
  int value = fallback;
  const auto* begin = s.data();
  const auto* end = s.data() + s.size();
  if (!s.empty() && (s[0] == '+')) ++begin;  // `# branch.ab +2 -0`
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{}) return fallback;
  (void)ptr;
  return value;
}

}  // namespace

proc::Result Repo::git(std::vector<std::string> args) const {
  std::vector<std::string> argv{"git", "-C", root_.string()};
  argv.insert(argv.end(), std::make_move_iterator(args.begin()),
              std::make_move_iterator(args.end()));
  return proc::run(argv);
}

std::optional<Repo> Repo::discover() {
  // Run without -C so git walks up from the process cwd.
  const auto r = proc::run({"git", "rev-parse", "--show-toplevel"});
  if (!r.ok()) return std::nullopt;

  const auto root = trim(r.out);
  if (root.empty()) return std::nullopt;
  return Repo(fs::path(std::string(root)));
}

std::optional<std::string> Repo::rev_parse(std::string_view rev) const {
  const auto r = git({"rev-parse", "--verify", "--quiet", std::string(rev)});
  if (!r.ok()) return std::nullopt;
  const auto oid = trim(r.out);
  if (oid.empty()) return std::nullopt;
  return std::string(oid);
}

std::optional<std::string> Repo::blob_oid_at(std::string_view rev, const fs::path& rel) const {
  // `rev:path` fails cleanly when the path does not exist in that tree.
  const auto spec = fmt::format("{}:{}", rev, rel.generic_string());
  const auto r = git({"rev-parse", "--verify", "--quiet", spec});
  if (!r.ok()) return std::nullopt;
  const auto oid = trim(r.out);
  if (oid.empty()) return std::nullopt;
  return std::string(oid);
}

std::optional<std::string> Repo::read_blob(std::string_view oid) const {
  auto r = git({"cat-file", "blob", std::string(oid)});
  if (!r.ok()) return std::nullopt;
  return std::move(r.out);
}

std::optional<std::string> Repo::read_file_at(std::string_view rev, const fs::path& rel) const {
  const auto oid = blob_oid_at(rev, rel);
  if (!oid) return std::nullopt;
  return read_blob(*oid);
}

std::vector<CommitInfo> Repo::log(int max, const fs::path& rel) const {
  std::vector<std::string> args{
      "log",
      fmt::format("--max-count={}", max),
      fmt::format("--format=%H{0}%an{0}%aI{0}%s{0}%P{1}", kUnitSep, kRecordSep),
  };
  if (!rel.empty()) {
    args.emplace_back("--");
    args.push_back(rel.generic_string());
  }

  const auto r = git(std::move(args));
  if (!r.ok()) return {};

  std::vector<CommitInfo> commits;
  for (auto record : split(r.out, kRecordSep)) {
    record = trim(record);
    if (record.empty()) continue;

    const auto fields = split(record, kUnitSep);
    if (fields.size() < 4) continue;

    CommitInfo commit{
        .oid = std::string(fields[0]),
        .author = std::string(fields[1]),
        .date = std::string(fields[2]),
        .subject = std::string(fields[3]),
        .parents = {},
    };

    if (fields.size() >= 5) {
      for (const auto parent : split(trim(fields[4]), ' ')) {
        if (!parent.empty()) commit.parents.emplace_back(parent);
      }
    }

    commits.push_back(std::move(commit));
  }
  return commits;
}

std::vector<BlobChange> Repo::changed_blobs(std::string_view from, std::string_view to) const {
  const auto r = git({"diff-tree", "-r", "-z", "--no-commit-id", std::string(from),
                      std::string(to)});
  if (!r.ok()) return {};

  // Each record is ":<srcmode> <dstmode> <srcsha> <dstsha> <status>" followed by a
  // NUL-separated path -- and a second path for renames and copies.
  const auto records = split(r.out, '\0');
  std::vector<BlobChange> changes;

  for (std::size_t i = 0; i + 1 < records.size(); ++i) {
    const auto meta = records[i];
    if (meta.empty() || meta.front() != ':') continue;

    const auto fields = split(meta.substr(1), ' ');
    if (fields.size() < 5) continue;

    BlobChange change;
    change.before_oid = std::string(fields[2]);
    change.after_oid = std::string(fields[3]);
    change.status = fields[4].empty() ? 'M' : fields[4].front();
    change.path = fs::path(std::string(records[++i]));

    // Renames and copies name their destination in a second path field.
    if ((change.status == 'R' || change.status == 'C') && i + 1 < records.size()) {
      change.path = fs::path(std::string(records[++i]));
    }

    changes.push_back(std::move(change));
  }

  return changes;
}

Status Repo::status() const {
  Status status;
  status.branch.head = "(unknown)";

  const auto r = git({"status", "--porcelain=v2", "--branch", "-z"});
  if (!r.ok()) return status;

  // With -z every record, headers included, is NUL-terminated.
  const auto records = split(r.out, '\0');

  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto rec = records[i];
    if (rec.empty()) continue;

    if (rec.starts_with("# branch.head ")) {
      status.branch.head = std::string(rec.substr(std::string_view("# branch.head ").size()));
      continue;
    }
    if (rec.starts_with("# branch.upstream ")) {
      status.branch.upstream =
          std::string(rec.substr(std::string_view("# branch.upstream ").size()));
      continue;
    }
    if (rec.starts_with("# branch.ab ")) {
      const auto ab = split(rec.substr(std::string_view("# branch.ab ").size()), ' ');
      if (ab.size() >= 2) {
        status.branch.ahead = parse_int(ab[0]);
        status.branch.behind = std::abs(parse_int(ab[1]));
      }
      continue;
    }
    if (rec.starts_with("# ")) continue;

    StatusEntry entry;
    switch (rec[0]) {
      case '1': {  // ordinary change: 8 fixed fields, then the path
        entry.xy = std::string(rec.substr(2, 2));
        entry.path = fs::path(std::string(after_nth_space(rec, 8)));
        break;
      }
      case '2': {  // rename/copy: one extra score field, and the source path follows
        entry.xy = std::string(rec.substr(2, 2));
        entry.path = fs::path(std::string(after_nth_space(rec, 9)));
        if (i + 1 < records.size()) {
          entry.orig_path = fs::path(std::string(records[++i]));
        }
        break;
      }
      case 'u': {  // unmerged: 10 fixed fields
        entry.xy = std::string(rec.substr(2, 2));
        entry.path = fs::path(std::string(after_nth_space(rec, 10)));
        break;
      }
      case '?': {
        entry.xy = "??";
        entry.untracked = true;
        entry.path = fs::path(std::string(after_nth_space(rec, 1)));
        break;
      }
      default:
        continue;  // '!' ignored entries, and anything unrecognised
    }

    if (!entry.path.empty()) {
      status.entries.push_back(std::move(entry));
    }
  }

  return status;
}

std::vector<fs::path> Repo::changed_paths(std::string_view from, std::string_view to) const {
  std::vector<std::string> args{"diff", "--name-only", "-z", std::string(from)};
  if (!to.empty()) {
    args.emplace_back(to);
  }

  const auto r = git(std::move(args));
  if (!r.ok()) return {};

  std::vector<fs::path> paths;
  for (const auto part : split(r.out, '\0')) {
    if (!part.empty()) paths.emplace_back(std::string(part));
  }
  return paths;
}

}  // namespace motif::git
