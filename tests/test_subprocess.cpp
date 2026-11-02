#include <catch2/catch_test_macros.hpp>

#include <string>

#include "motif/git/subprocess.h"

using motif::proc::run;

TEST_CASE("run captures stdout and the exit code", "[proc]") {
  const auto r = run({"echo", "hello"});
  REQUIRE(r.ok());
  REQUIRE(r.exit_code == 0);
  REQUIRE(r.out == "hello\n");
  REQUIRE(r.err.empty());
}

TEST_CASE("run reports a non-zero exit code", "[proc]") {
  const auto r = run({"false"});
  REQUIRE_FALSE(r.ok());
  REQUIRE(r.exit_code == 1);
}

TEST_CASE("run separates stderr from stdout", "[proc]") {
  const auto r = run({"sh", "-c", "printf out; printf err >&2"});
  REQUIRE(r.ok());
  REQUIRE(r.out == "out");
  REQUIRE(r.err == "err");
}

TEST_CASE("arguments are passed verbatim without shell interpretation", "[proc]") {
  // Score filenames contain spaces, and must never be re-split or glob-expanded.
  const auto r = run({"printf", "%s\n", "Piano Concerto in G Minor.mscz"});
  REQUIRE(r.ok());
  REQUIRE(r.out == "Piano Concerto in G Minor.mscz\n");

  const auto glob = run({"printf", "%s", "*"});
  REQUIRE(glob.ok());
  REQUIRE(glob.out == "*");
}

TEST_CASE("run survives output larger than a pipe buffer", "[proc]") {
  // A .mscz blob is megabytes; draining only one stream at a time would deadlock here.
  const auto r = run({"sh", "-c", "yes abcdefghij | head -c 2000000; echo hi >&2"});
  REQUIRE(r.ok());
  REQUIRE(r.out.size() == 2000000);
  REQUIRE(r.err == "hi\n");
}

TEST_CASE("run handles binary output containing NUL bytes", "[proc]") {
  const auto r = run({"printf", "a\\000b"});
  REQUIRE(r.ok());
  REQUIRE(r.out == std::string("a\0b", 3));
}

TEST_CASE("run reports a missing executable without throwing", "[proc]") {
  const auto r = run({"motif-no-such-program-exists"});
  REQUIRE_FALSE(r.ok());
  REQUIRE_FALSE(r.err.empty());
}
