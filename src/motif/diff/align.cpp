#include "motif/diff/align.h"

#include <fmt/format.h>

#include <algorithm>
#include <functional>

namespace motif::diff {
namespace {

/// Guard on the quadratic alignment step.
///
/// Beyond this many measures on either side *after* trimming, the edit is too large for
/// an LCS table to be worth building; alignment falls back to positional matching, which
/// still produces a usable (if coarser) result.
constexpr std::size_t kMaxLcsDimension = 3000;

/// Positional fallback: pair measures up in order, then report the tail as inserted or
/// deleted. Used only when the changed region is too large for an LCS.
void append_positional(Alignment& out, std::size_t b_begin, std::size_t b_end,
                       std::size_t a_begin, std::size_t a_end) {
  const std::size_t common = std::min(b_end - b_begin, a_end - a_begin);
  for (std::size_t k = 0; k < common; ++k) {
    out.push_back({MeasureOp::Match, static_cast<int>(b_begin + k), static_cast<int>(a_begin + k)});
  }
  for (std::size_t k = common; k < b_end - b_begin; ++k) {
    out.push_back({MeasureOp::Delete, static_cast<int>(b_begin + k), -1});
  }
  for (std::size_t k = common; k < a_end - a_begin; ++k) {
    out.push_back({MeasureOp::Insert, -1, static_cast<int>(a_begin + k)});
  }
}

/// Turns deleted-then-inserted bars into edited ones.
///
/// Matching by content, an edited bar can only appear as one bar deleted and another
/// inserted in its place. Within each stretch between matches, bars deleted and bars
/// inserted are paired off in order as the same bar, changed; only the surplus on one
/// side is a real insertion or deletion.
Alignment pair_replacements(const Alignment& alignment) {
  Alignment out;
  out.reserve(alignment.size());
  for (std::size_t i = 0; i < alignment.size();) {
    if (alignment[i].op == MeasureOp::Match) {
      out.push_back(alignment[i++]);
      continue;
    }
    std::vector<int> deleted;
    std::vector<int> inserted;
    for (; i < alignment.size() && alignment[i].op != MeasureOp::Match; ++i) {
      if (alignment[i].op == MeasureOp::Delete) {
        deleted.push_back(alignment[i].before);
      } else {
        inserted.push_back(alignment[i].after);
      }
    }
    const std::size_t paired = std::min(deleted.size(), inserted.size());
    for (std::size_t k = 0; k < paired; ++k) {
      out.push_back({MeasureOp::Match, deleted[k], inserted[k]});
    }
    for (std::size_t k = paired; k < deleted.size(); ++k) {
      out.push_back({MeasureOp::Delete, deleted[k], -1});
    }
    for (std::size_t k = paired; k < inserted.size(); ++k) {
      out.push_back({MeasureOp::Insert, -1, inserted[k]});
    }
  }
  return out;
}

}  // namespace

std::string measure_fingerprint(const score::Measure& measure) {
  // A textual fingerprint rather than a numeric hash: collisions would silently hide
  // real changes, and these strings are short-lived and few.
  std::string out;
  out.reserve(64);

  if (measure.time_sig) {
    out += fmt::format("T{}/{};", measure.time_sig->n, measure.time_sig->d);
  }
  if (measure.key_sig) {
    out += fmt::format("K{};", measure.key_sig->concert);
  }
  if (measure.clef) {
    out += fmt::format("C{};", measure.clef->concert);
  }
  out += fmt::format("L{}/{};", measure.length.num, measure.length.den);

  for (const auto& voice : measure.voices) {
    out += fmt::format("v{}:", voice.index);
    for (const auto& el : voice.elements) {
      out += fmt::format("{}@{}+{}", el.is_chord() ? 'c' : 'r', el.onset, el.ticks);
      for (const auto& note : el.notes) {
        out += fmt::format("[{}/{}]", note.pitch.midi, note.pitch.tpc);
      }
      for (const auto& artic : el.articulations) {
        out += fmt::format("<{}>", artic);
      }
      out += ',';
    }
    out += ';';
  }
  return out;
}

bool measures_differ(const score::Measure& a, const score::Measure& b) {
  return measure_fingerprint(a) != measure_fingerprint(b);
}

std::string measure_token(const score::Measure& measure) {
  if (!measure.eid.empty()) return "e:" + measure.eid;
  return "h:" + measure_fingerprint(measure);
}

Alignment align_tokens(const std::vector<std::string>& before,
                       const std::vector<std::string>& after) {
  Alignment out;
  const std::size_t nb = before.size();
  const std::size_t na = after.size();

  // Trim the common prefix.
  std::size_t prefix = 0;
  while (prefix < nb && prefix < na && before[prefix] == after[prefix]) {
    out.push_back({MeasureOp::Match, static_cast<int>(prefix), static_cast<int>(prefix)});
    ++prefix;
  }

  // Trim the common suffix.
  std::size_t suffix = 0;
  while (suffix < nb - prefix && suffix < na - prefix &&
         before[nb - 1 - suffix] == after[na - 1 - suffix]) {
    ++suffix;
  }

  const std::size_t b_lo = prefix;
  const std::size_t b_hi = nb - suffix;
  const std::size_t a_lo = prefix;
  const std::size_t a_hi = na - suffix;
  const std::size_t bn = b_hi - b_lo;
  const std::size_t an = a_hi - a_lo;

  if (bn == 0 || an == 0 || bn > kMaxLcsDimension || an > kMaxLcsDimension) {
    append_positional(out, b_lo, b_hi, a_lo, a_hi);
  } else {
    // Longest common subsequence over the changed region.
    std::vector<std::vector<int>> lcs(bn + 1, std::vector<int>(an + 1, 0));
    for (std::size_t i = bn; i-- > 0;) {
      for (std::size_t j = an; j-- > 0;) {
        lcs[i][j] = (before[b_lo + i] == after[a_lo + j])
                        ? lcs[i + 1][j + 1] + 1
                        : std::max(lcs[i + 1][j], lcs[i][j + 1]);
      }
    }

    std::size_t i = 0;
    std::size_t j = 0;
    while (i < bn && j < an) {
      if (before[b_lo + i] == after[a_lo + j]) {
        out.push_back({MeasureOp::Match, static_cast<int>(b_lo + i), static_cast<int>(a_lo + j)});
        ++i;
        ++j;
      } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
        out.push_back({MeasureOp::Delete, static_cast<int>(b_lo + i), -1});
        ++i;
      } else {
        out.push_back({MeasureOp::Insert, -1, static_cast<int>(a_lo + j)});
        ++j;
      }
    }
    for (; i < bn; ++i) {
      out.push_back({MeasureOp::Delete, static_cast<int>(b_lo + i), -1});
    }
    for (; j < an; ++j) {
      out.push_back({MeasureOp::Insert, -1, static_cast<int>(a_lo + j)});
    }
  }

  // Replay the trimmed suffix.
  for (std::size_t k = 0; k < suffix; ++k) {
    out.push_back({MeasureOp::Match, static_cast<int>(b_hi + k), static_cast<int>(a_hi + k)});
  }
  return out;
}

Alignment align_score(const score::Score& before, const score::Score& after,
                      const std::vector<std::pair<int, int>>& staff_pairs) {
  const auto eid_coverage = [](const score::Staff& staff) {
    if (staff.measures.empty()) return 0.0;
    std::size_t with_eid = 0;
    for (const auto& m : staff.measures) {
      if (!m.eid.empty()) ++with_eid;
    }
    return static_cast<double>(with_eid) / static_cast<double>(staff.measures.size());
  };

  // Prefer a staff pair where both sides carry measure eids: identity matching survives
  // edits, so a modified bar still aligns with its old self.
  for (const auto& [before_id, after_id] : staff_pairs) {
    const auto* bs = before.staff_by_id(before_id);
    const auto* as = after.staff_by_id(after_id);
    if (bs == nullptr || as == nullptr) continue;
    if (eid_coverage(*bs) < 0.5 || eid_coverage(*as) < 0.5) continue;

    std::vector<std::string> b_tokens;
    std::vector<std::string> a_tokens;
    b_tokens.reserve(bs->measures.size());
    a_tokens.reserve(as->measures.size());
    for (const auto& m : bs->measures) b_tokens.push_back(measure_token(m));
    for (const auto& m : as->measures) a_tokens.push_back(measure_token(m));
    return align_tokens(b_tokens, a_tokens);
  }

  // No eids available. Fingerprint each measure across every matched staff, so the token
  // describes the whole vertical slice rather than one instrument's line.
  const auto slice_tokens = [&staff_pairs](const score::Score& s, bool use_first) {
    std::vector<std::string> tokens;
    if (s.staves.empty()) return tokens;

    tokens.resize(s.staves.front().measures.size());
    for (const auto& pair : staff_pairs) {
      const auto* staff = s.staff_by_id(use_first ? pair.first : pair.second);
      if (staff == nullptr) continue;
      for (std::size_t i = 0; i < tokens.size() && i < staff->measures.size(); ++i) {
        tokens[i] += measure_fingerprint(staff->measures[i]);
        tokens[i] += '|';
      }
    }
    return tokens;
  };

  return pair_replacements(align_tokens(slice_tokens(before, true), slice_tokens(after, false)));
}

}  // namespace motif::diff
