#include "motif/render/pager.h"

#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace motif::render {
namespace {

bool g_initialised = false;
bool g_was_tty = false;
int g_width = 0;

pid_t g_pager_pid = -1;

const char* env_or_null(const char* name) {
  const char* value = std::getenv(name);
  return (value != nullptr && value[0] != '\0') ? value : nullptr;
}

}  // namespace

void init_terminal() {
  if (g_initialised) return;
  g_initialised = true;

  g_was_tty = ::isatty(STDOUT_FILENO) == 1;

  // An explicit COLUMNS wins, which is what makes output reproducible in tests and
  // scripts where there is no terminal to measure.
  if (const char* columns = env_or_null("COLUMNS")) {
    const int parsed = std::atoi(columns);
    if (parsed > 0) {
      g_width = parsed;
      return;
    }
  }

  if (!g_was_tty) return;

  winsize ws{};
  if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    g_width = ws.ws_col;
  }
}

bool stdout_was_tty() {
  init_terminal();
  return g_was_tty;
}

int captured_width() {
  init_terminal();
  return g_width;
}

bool start_pager() {
  init_terminal();

  if (!g_was_tty || g_pager_pid > 0) return false;
  if (env_or_null("MOTIF_NO_PAGER") != nullptr) return false;

  const char* pager = env_or_null("MOTIF_PAGER");
  if (pager == nullptr) pager = env_or_null("PAGER");
  if (pager == nullptr) pager = "less";
  if (std::strcmp(pager, "cat") == 0) return false;

  // Quit if the output fits on one screen, keep ANSI colour, and do not clear the
  // screen on exit -- the same defaults git installs.
  if (env_or_null("LESS") == nullptr) {
    ::setenv("LESS", "FRX", 1);
  }

  int fds[2];
  if (::pipe(fds) != 0) return false;

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(fds[0]);
    ::close(fds[1]);
    return false;
  }

  if (pid == 0) {
    // Child: read the pipe as stdin and become the pager.
    ::dup2(fds[0], STDIN_FILENO);
    ::close(fds[0]);
    ::close(fds[1]);
    ::execlp("/bin/sh", "sh", "-c", pager, nullptr);
    ::_exit(127);
  }

  // Parent: send everything we print into the pipe.
  std::fflush(stdout);
  ::dup2(fds[1], STDOUT_FILENO);
  ::close(fds[0]);
  ::close(fds[1]);

  g_pager_pid = pid;
  return true;
}

void finish_pager() {
  if (g_pager_pid <= 0) return;

  std::fflush(stdout);
  ::close(STDOUT_FILENO);

  int status = 0;
  while (::waitpid(g_pager_pid, &status, 0) < 0 && errno == EINTR) {
    // retry
  }
  g_pager_pid = -1;
}

}  // namespace motif::render
