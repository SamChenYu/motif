#include <catch2/catch_test_macros.hpp>

#include <string>

#include "motif/git/subprocess.h"

using motif::proc::run;

namespace {

// tests/support/child.cpp, built next to the test binary.
const std::string kChild = MOTIF_TEST_CHILD;

}  // namespace

TEST_CASE("run captures stdout and the exit code", "[proc]") {
  const auto r = run({kChild, "args", "hello"});
  REQUIRE(r.ok());
  REQUIRE(r.exit_code == 0);
  REQUIRE(r.out == "hello\n");
  REQUIRE(r.err.empty());
}

TEST_CASE("run reports a non-zero exit code", "[proc]") {
  const auto r = run({kChild, "exit", "3"});
  REQUIRE_FALSE(r.ok());
  REQUIRE(r.exit_code == 3);
}

TEST_CASE("run separates stderr from stdout", "[proc]") {
  const auto r = run({kChild, "split"});
  REQUIRE(r.ok());
  REQUIRE(r.out == "out");
  REQUIRE(r.err == "err");
}

TEST_CASE("arguments are passed verbatim without shell interpretation", "[proc]") {
  // Score filenames contain spaces, and must never be re-split or glob-expanded. Windows
  // re-splits a single command line in the child, so quotes and backslashes are the
  // cases that matter there.
  const auto r = run({kChild, "args", "Piano Concerto in G Minor.mscz", "*", "say \"hi\"",
                      "dir\\", "a\\\\\"b", "", "$HOME"});
  REQUIRE(r.ok());
  REQUIRE(r.out ==
          "Piano Concerto in G Minor.mscz\n*\nsay \"hi\"\ndir\\\na\\\\\"b\n\n$HOME\n");
}

TEST_CASE("run survives output larger than a pipe buffer", "[proc]") {
  // A .mscz blob is megabytes; draining only one stream at a time would deadlock here.
  const auto r = run({kChild, "big", "2000000"});
  REQUIRE(r.ok());
  REQUIRE(r.out.size() == 2000000);
  REQUIRE(r.err == "hi\n");
}

TEST_CASE("run handles binary output containing NUL bytes", "[proc]") {
  const auto r = run({kChild, "nul"});
  REQUIRE(r.ok());
  REQUIRE(r.out == std::string("a\0b", 3));
}

TEST_CASE("run reports a missing executable without throwing", "[proc]") {
  const auto r = run({"motif-no-such-program-exists"});
  REQUIRE_FALSE(r.ok());
  REQUIRE_FALSE(r.err.empty());
}

TEST_CASE("self_executable names the running test binary", "[proc]") {
  const auto self = motif::proc::self_executable();
  REQUIRE_FALSE(self.empty());
  REQUIRE(self.stem().string() == "motif_tests");
}
