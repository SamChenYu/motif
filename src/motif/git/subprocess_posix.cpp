#include "motif/git/subprocess.h"

#include <fmt/format.h>
#include <poll.h>
#include <spawn.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace motif::proc {
namespace {

/// Builds the NULL-terminated char* array execvp/posix_spawn wants.
///
/// The pointers alias `argv`'s strings, so the returned vector must not outlive it.
std::vector<char*> to_c_argv(const std::vector<std::string>& argv) {
  std::vector<char*> c;
  c.reserve(argv.size() + 1);
  for (const auto& a : argv) {
    c.push_back(const_cast<char*>(a.c_str()));
  }
  c.push_back(nullptr);
  return c;
}

/// RAII wrapper for a pipe pair, so early returns can't leak descriptors.
class Pipe {
 public:
  Pipe() {
    if (::pipe(fds_.data()) != 0) {
      fds_ = {-1, -1};
    }
  }
  ~Pipe() {
    close_read();
    close_write();
  }
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;

  [[nodiscard]] bool valid() const { return fds_[0] >= 0 && fds_[1] >= 0; }
  [[nodiscard]] int read_fd() const { return fds_[0]; }
  [[nodiscard]] int write_fd() const { return fds_[1]; }

  void close_read() {
    if (fds_[0] >= 0) {
      ::close(fds_[0]);
      fds_[0] = -1;
    }
  }
  void close_write() {
    if (fds_[1] >= 0) {
      ::close(fds_[1]);
      fds_[1] = -1;
    }
  }

 private:
  std::array<int, 2> fds_{-1, -1};
};

/// Drains both pipes concurrently.
///
/// Reading one to EOF before starting the other would deadlock as soon as a child fills
/// the other pipe's buffer -- and `git cat-file blob` on a 2 MB score does exactly that.
void drain(int out_fd, int err_fd, std::string& out, std::string& err) {
  std::array<pollfd, 2> pfds{
      pollfd{out_fd, POLLIN, 0},
      pollfd{err_fd, POLLIN, 0},
  };
  std::array<std::string*, 2> sinks{&out, &err};
  int open_count = 2;

  std::array<char, 64 * 1024> buf{};
  while (open_count > 0) {
    if (::poll(pfds.data(), pfds.size(), -1) < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (std::size_t i = 0; i < pfds.size(); ++i) {
      if (pfds[i].fd < 0 || pfds[i].revents == 0) continue;

      const ssize_t n = ::read(pfds[i].fd, buf.data(), buf.size());
      if (n > 0) {
        sinks[i]->append(buf.data(), static_cast<std::size_t>(n));
      } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
        // EOF, or an unrecoverable error: stop polling this descriptor.
        pfds[i].fd = -1;
        --open_count;
      }
    }
  }
}

}  // namespace

Result run(const std::vector<std::string>& argv) {
  Result r;
  if (argv.empty()) {
    r.err = "motif: internal error: empty argv";
    return r;
  }

  Pipe out_pipe;
  Pipe err_pipe;
  if (!out_pipe.valid() || !err_pipe.valid()) {
    r.err = fmt::format("motif: pipe failed: {}", std::strerror(errno));
    return r;
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    r.err = "motif: posix_spawn_file_actions_init failed";
    return r;
  }

  // In the child: point stdout/stderr at the write ends, then drop every pipe fd so the
  // child holds no copy of a read end (which would keep our reads from ever seeing EOF).
  posix_spawn_file_actions_adddup2(&actions, out_pipe.write_fd(), STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, err_pipe.write_fd(), STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, out_pipe.read_fd());
  posix_spawn_file_actions_addclose(&actions, err_pipe.read_fd());
  posix_spawn_file_actions_addclose(&actions, out_pipe.write_fd());
  posix_spawn_file_actions_addclose(&actions, err_pipe.write_fd());

  auto c_argv = to_c_argv(argv);
  pid_t pid = -1;
  const int spawn_rc =
      ::posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr, c_argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);

  if (spawn_rc != 0) {
    r.err = fmt::format("motif: failed to run '{}': {}", argv[0], std::strerror(spawn_rc));
    return r;
  }

  // The parent must release the write ends or the reads below never reach EOF.
  out_pipe.close_write();
  err_pipe.close_write();

  drain(out_pipe.read_fd(), err_pipe.read_fd(), r.out, r.err);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    // retry
  }

  if (WIFEXITED(status)) {
    r.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    r.exit_code = 128 + WTERMSIG(status);
  }
  return r;
}

Result run_attached(const std::vector<std::string>& argv) {
  Result r;
  if (argv.empty()) {
    r.err = "motif: internal error: empty argv";
    return r;
  }

  // As system() does: while the child owns the terminal, Ctrl+C is its business alone.
  struct sigaction ignore {};
  struct sigaction old_int {};
  struct sigaction old_quit {};
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  ::sigaction(SIGINT, &ignore, &old_int);
  ::sigaction(SIGQUIT, &ignore, &old_quit);

  // The child must start with the default dispositions, not our ignored ones.
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  sigset_t defaults;
  sigemptyset(&defaults);
  sigaddset(&defaults, SIGINT);
  sigaddset(&defaults, SIGQUIT);
  posix_spawnattr_setsigdefault(&attr, &defaults);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

  std::fflush(stdout);
  std::fflush(stderr);

  auto c_argv = to_c_argv(argv);
  pid_t pid = -1;
  const int spawn_rc =
      ::posix_spawnp(&pid, argv[0].c_str(), nullptr, &attr, c_argv.data(), environ);
  posix_spawnattr_destroy(&attr);

  if (spawn_rc != 0) {
    r.err = fmt::format("motif: failed to run '{}': {}", argv[0], std::strerror(spawn_rc));
  } else {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      // retry
    }
    if (WIFEXITED(status)) {
      r.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      r.exit_code = 128 + WTERMSIG(status);
    }
  }

  ::sigaction(SIGINT, &old_int, nullptr);
  ::sigaction(SIGQUIT, &old_quit, nullptr);
  return r;
}

std::filesystem::path self_executable() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
  std::error_code ec;
  auto path = std::filesystem::canonical(buf.c_str(), ec);
  return ec ? std::filesystem::path(buf.c_str()) : path;
#else
  std::error_code ec;
  auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path() : path;
#endif
}

void exec(const std::vector<std::string>& argv) {
  if (argv.empty()) {
    std::_Exit(127);
  }
  auto c_argv = to_c_argv(argv);
  ::execvp(argv[0].c_str(), c_argv.data());

  // execvp only returns on failure.
  fmt::print(stderr, "motif: failed to exec '{}': {}\n", argv[0], std::strerror(errno));
  std::_Exit(127);
}

}  // namespace motif::proc
