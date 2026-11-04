#include "motif/render/pager.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace motif::render {
namespace {

bool g_initialised = false;
bool g_was_tty = false;
bool g_stdin_tty = false;
int g_width = 0;

const char* env_or_null(const char* name) {
  const char* value = std::getenv(name);
  return (value != nullptr && value[0] != '\0') ? value : nullptr;
}

#ifdef _WIN32

FILE* g_pager = nullptr;

UINT g_saved_output_cp = 0;
DWORD g_saved_out_mode = 0;
bool g_restore_mode = false;

void restore_console() {
  if (g_restore_mode) ::SetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), g_saved_out_mode);
  if (g_saved_output_cp != 0) ::SetConsoleOutputCP(g_saved_output_cp);
}

/// Has the console understand ANSI escapes and UTF-8, as every POSIX terminal does, and
/// writes "\n" as it is rather than as "\r\n", so piped output matches other platforms.
void prepare_console() {
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);

  if (!g_was_tty) return;
  const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (::GetConsoleMode(out, &mode)) {
    g_saved_out_mode = mode;
    g_restore_mode = ::SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
  }
  g_saved_output_cp = ::GetConsoleOutputCP();
  ::SetConsoleOutputCP(CP_UTF8);
  std::atexit(restore_console);
}

int terminal_width() {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info)) return 0;
  return info.srWindow.Right - info.srWindow.Left + 1;
}

/// True when `name` (a bare program name) can be found on PATH.
bool on_path(const char* name) {
  char found[MAX_PATH];
  return ::SearchPathA(nullptr, name, ".exe", MAX_PATH, found, nullptr) > 0;
}

#else

pid_t g_pager_pid = -1;

int terminal_width() {
  winsize ws{};
  if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
  return 0;
}

#endif

}  // namespace

void init_terminal() {
  if (g_initialised) return;
  g_initialised = true;

#ifdef _WIN32
  g_was_tty = _isatty(_fileno(stdout)) != 0;
  g_stdin_tty = _isatty(_fileno(stdin)) != 0;
  prepare_console();
#else
  g_was_tty = ::isatty(STDOUT_FILENO) == 1;
  g_stdin_tty = ::isatty(STDIN_FILENO) == 1;
#endif

  // An explicit COLUMNS wins, which is what makes output reproducible in tests and
  // scripts where there is no terminal to measure.
  if (const char* columns = env_or_null("COLUMNS")) {
    const int parsed = std::atoi(columns);
    if (parsed > 0) {
      g_width = parsed;
      return;
    }
  }

  if (g_was_tty) g_width = terminal_width();
}

bool stdout_was_tty() {
  init_terminal();
  return g_was_tty;
}

bool stdin_was_tty() {
  init_terminal();
  return g_stdin_tty;
}

int captured_width() {
  init_terminal();
  return g_width;
}

bool start_pager() {
  init_terminal();

#ifdef _WIN32
  if (!g_was_tty || g_pager != nullptr) return false;
#else
  if (!g_was_tty || g_pager_pid > 0) return false;
#endif
  if (env_or_null("MOTIF_NO_PAGER") != nullptr) return false;

  const char* pager = env_or_null("MOTIF_PAGER");
  if (pager == nullptr) pager = env_or_null("PAGER");
  if (pager == nullptr) pager = "less";
  if (std::strcmp(pager, "cat") == 0) return false;

  // Quit if the output fits on one screen, keep ANSI colour, and do not clear the
  // screen on exit -- the same defaults git installs.
  if (env_or_null("LESS") == nullptr) {
#ifdef _WIN32
    _putenv_s("LESS", "FRX");
#else
    ::setenv("LESS", "FRX", 1);
#endif
  }

#ifdef _WIN32
  // Windows has no pager of its own; Git for Windows ships less, but rarely on PATH.
  if (std::strcmp(pager, "less") == 0 && !on_path("less")) return false;

  std::fflush(stdout);
  g_pager = _popen(pager, "wb");
  if (g_pager == nullptr) return false;
  // Send everything we print into the pager's stdin.
  _dup2(_fileno(g_pager), _fileno(stdout));
  return true;
#else
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
#endif
}

void finish_pager() {
#ifdef _WIN32
  if (g_pager == nullptr) return;

  // Both our stdout and the pager's own stream hold the pipe open; the pager only sees
  // the end of its input once both are closed.
  std::fflush(stdout);
  _close(_fileno(stdout));
  _pclose(g_pager);
  g_pager = nullptr;
#else
  if (g_pager_pid <= 0) return;

  std::fflush(stdout);
  ::close(STDOUT_FILENO);

  int status = 0;
  while (::waitpid(g_pager_pid, &status, 0) < 0 && errno == EINTR) {
    // retry
  }
  g_pager_pid = -1;
#endif
}

}  // namespace motif::render
