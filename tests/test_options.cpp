#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "motif/cli/options.h"

using motif::cli::parse_options;

namespace {

/// Parses a command line written the way a user would type it.
motif::cli::Options parse(std::initializer_list<const char*> words, bool revisions = true) {
  std::vector<std::string> args{"diff"};
  for (const char* word : words) args.emplace_back(word);
  return parse_options(args, revisions);
}

}  // namespace

TEST_CASE("an empty command line parses to defaults", "[options]") {
  const auto options = parse({});
  CHECK(options.ok());
  CHECK_FALSE(options.instrument.has_value());
  CHECK_FALSE(options.measures.has_value());
  CHECK_FALSE(options.ascii);
  CHECK(options.positional.empty());
}

TEST_CASE("flag values are accepted in both spellings", "[options]") {
  CHECK(*parse({"--instrument", "violin"}).instrument == "violin");
  CHECK(*parse({"--instrument=violin"}).instrument == "violin");
  CHECK(*parse({"--staff", "3"}).staff == 3);
  CHECK(*parse({"--staff=3"}).staff == 3);
}

TEST_CASE("measure ranges are parsed and validated", "[options]") {
  const auto range = parse({"--measures", "84:104"});
  REQUIRE(range.ok());
  REQUIRE(range.measures.has_value());
  CHECK(range.measures->first == 84);
  CHECK(range.measures->second == 104);

  // A single measure is a range of one.
  const auto single = parse({"--measures", "84"});
  REQUIRE(single.measures.has_value());
  CHECK(single.measures->first == 84);
  CHECK(single.measures->second == 84);

  CHECK_FALSE(parse({"--measures", "104:84"}).ok());  // backwards
  CHECK_FALSE(parse({"--measures", "abc"}).ok());
  CHECK_FALSE(parse({"--measures"}).ok());  // missing value
}

TEST_CASE("revisions are collected positionally", "[options]") {
  const auto two = parse({"HEAD~2", "HEAD"});
  REQUIRE(two.ok());
  CHECK(two.positional == std::vector<std::string>{"HEAD~2", "HEAD"});

  const auto mixed = parse({"--instrument", "horn", "HEAD~1", "--ascii"});
  REQUIRE(mixed.ok());
  CHECK(mixed.positional == std::vector<std::string>{"HEAD~1"});
  CHECK(mixed.ascii);
  CHECK(*mixed.instrument == "horn");
}

TEST_CASE("everything after -- is a path", "[options]") {
  const auto options = parse({"HEAD", "--", "scores/concerto.mscz", "--not-a-flag"});
  REQUIRE(options.ok());
  CHECK(options.positional == std::vector<std::string>{"HEAD"});
  CHECK(options.paths ==
        std::vector<std::string>{"scores/concerto.mscz", "--not-a-flag"});
}

TEST_CASE("commit counts accept git's spellings", "[options]") {
  CHECK(parse({"-1"}).limit == 1);
  CHECK(parse({"-n", "5"}).limit == 5);
  CHECK(parse({"--max-count=5"}).limit == 5);
  CHECK(parse({"-20"}).limit == 20);
}

TEST_CASE("unknown options are rejected rather than ignored", "[options]") {
  const auto options = parse({"--nonsense"});
  CHECK_FALSE(options.ok());
  CHECK(options.error->find("--nonsense") != std::string::npos);
}

TEST_CASE("commands that take no revision reject stray words", "[options]") {
  CHECK_FALSE(parse({"HEAD"}, /*revisions=*/false).ok());
  CHECK(parse({"--ascii"}, /*revisions=*/false).ok());
}

TEST_CASE("help is recognised before anything else can fail", "[options]") {
  CHECK(parse({"--help"}).help);
  CHECK(parse({"-h"}).help);
}

TEST_CASE("filters reach the diff engine", "[options]") {
  const auto options = parse({"--instrument", "violin", "--measures", "10:20", "--staff", "4"});
  const auto engine = options.to_diff_options();
  CHECK(*engine.instrument == "violin");
  CHECK(engine.measures->first == 10);
  CHECK(*engine.staff == 4);
}

TEST_CASE("section selectors and display flags are independent", "[options]") {
  const auto options = parse({"--orchestration", "--key", "--stat", "--ascii", "--no-color"});
  CHECK(options.only_orchestration);
  CHECK(options.only_key);
  CHECK(options.stat);
  CHECK(options.ascii);
  CHECK(options.no_color);

  const auto style = options.style();
  CHECK_FALSE(style.color);
  CHECK_FALSE(style.unicode);
}
