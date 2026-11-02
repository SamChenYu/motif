#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "motif/diff/align.h"

using motif::diff::align_tokens;
using motif::diff::MeasureOp;

namespace {

/// Compact rendering of an alignment: "=" match, "+" insert, "-" delete.
std::string shape(const motif::diff::Alignment& alignment) {
  std::string out;
  for (const auto& link : alignment) {
    switch (link.op) {
      case MeasureOp::Match: out += '='; break;
      case MeasureOp::Insert: out += '+'; break;
      case MeasureOp::Delete: out += '-'; break;
    }
  }
  return out;
}

std::vector<std::string> tokens(std::initializer_list<const char*> items) {
  return {items.begin(), items.end()};
}

}  // namespace

TEST_CASE("identical sequences align entirely as matches", "[align]") {
  const auto a = align_tokens(tokens({"a", "b", "c"}), tokens({"a", "b", "c"}));
  CHECK(shape(a) == "===");
  CHECK(a[1].before == 1);
  CHECK(a[1].after == 1);
}

TEST_CASE("an insertion is reported without disturbing its neighbours", "[align]") {
  // This is the property the whole design rests on: adding a bar must not make every
  // later bar look changed.
  const auto a = align_tokens(tokens({"a", "b", "c", "d"}), tokens({"a", "b", "x", "c", "d"}));
  CHECK(shape(a) == "==+==");

  // The measures after the insertion still point at their originals.
  CHECK(a[3].op == MeasureOp::Match);
  CHECK(a[3].before == 2);
  CHECK(a[3].after == 3);
  CHECK(a[4].before == 3);
  CHECK(a[4].after == 4);
}

TEST_CASE("a deletion is reported", "[align]") {
  const auto a = align_tokens(tokens({"a", "b", "c", "d"}), tokens({"a", "c", "d"}));
  CHECK(shape(a) == "=-==");
  CHECK(a[1].before == 1);
  CHECK(a[1].after == -1);
}

TEST_CASE("a replacement becomes a deletion plus an insertion", "[align]") {
  const auto a = align_tokens(tokens({"a", "b", "c"}), tokens({"a", "x", "c"}));
  CHECK((shape(a) == "=-+=" || shape(a) == "=+-="));
}

TEST_CASE("empty sequences are handled", "[align]") {
  CHECK(shape(align_tokens({}, {})).empty());
  CHECK(shape(align_tokens({}, tokens({"a", "b"}))) == "++");
  CHECK(shape(align_tokens(tokens({"a", "b"}), {})) == "--");
}

TEST_CASE("wholly different sequences align as delete-all then insert-all", "[align]") {
  const auto a = align_tokens(tokens({"a", "b"}), tokens({"x", "y"}));
  CHECK(a.size() == 4);
  int matches = 0;
  for (const auto& link : a) {
    if (link.op == MeasureOp::Match) ++matches;
  }
  CHECK(matches == 0);
}

TEST_CASE("repeated tokens align without drift", "[align]") {
  // Whole-measure rests fingerprint identically, so long runs of equal tokens are the
  // common case in real scores.
  const auto a = align_tokens(tokens({"r", "r", "r", "r"}), tokens({"r", "r", "r", "r", "r"}));
  CHECK(shape(a).find('+') != std::string::npos);
  int matches = 0;
  for (const auto& link : a) {
    if (link.op == MeasureOp::Match) ++matches;
  }
  CHECK(matches == 4);
}
