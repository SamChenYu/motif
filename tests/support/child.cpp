// A stand-in child process for the subprocess tests, so they run the same everywhere
// rather than leaning on sh, printf and yes.
//
//   child args A B ...   prints each argument on its own line
//   child exit N         exits with status N
//   child split          writes "out" to stdout and "err" to stderr
//   child big N          writes N bytes to stdout, then "hi" to stderr
//   child nul            writes "a", a NUL byte and "b"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  if (argc < 2) return 2;
  const std::string mode = argv[1];

  if (mode == "args") {
    for (int i = 2; i < argc; ++i) std::printf("%s\n", argv[i]);
    return 0;
  }
  if (mode == "exit" && argc > 2) return std::atoi(argv[2]);
  if (mode == "split") {
    std::fputs("out", stdout);
    std::fflush(stdout);
    std::fputs("err", stderr);
    return 0;
  }
  if (mode == "big" && argc > 2) {
    const long total = std::atol(argv[2]);
    const char pattern[] = "abcdefghij\n";
    for (long i = 0; i < total; ++i) std::fputc(pattern[i % 11], stdout);
    std::fflush(stdout);
    std::fputs("hi\n", stderr);
    return 0;
  }
  if (mode == "nul") {
    std::fwrite("a\0b", 1, 3, stdout);
    return 0;
  }
  return 2;
}
