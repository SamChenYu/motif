#include "motif/git/subprocess.h"

#include <fmt/format.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

namespace motif::proc {
namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string error_text(DWORD code) {
  char* buf = nullptr;
  const DWORD len = ::FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<char*>(&buf), 0, nullptr);
  std::string text = len > 0 ? std::string(buf, len) : fmt::format("error {}", code);
  ::LocalFree(buf);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.')) {
    text.pop_back();
  }
  return text;
}

/// Quotes one argument so that the child's C runtime splits it back out unchanged.
///
/// Windows passes a single command line, which each program re-splits itself; this
/// follows the rules the Microsoft C runtime (and CommandLineToArgvW) use.
std::wstring quote(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;

  std::wstring out = L"\"";
  for (auto it = arg.begin();; ++it) {
    std::size_t backslashes = 0;
    while (it != arg.end() && *it == L'\\') {
      ++it;
      ++backslashes;
    }
    if (it == arg.end()) {
      // Backslashes before the closing quote must be doubled, or they would escape it.
      out.append(backslashes * 2, L'\\');
      break;
    }
    if (*it == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
    } else {
      out.append(backslashes, L'\\');
    }
    out.push_back(*it);
  }
  out.push_back(L'"');
  return out;
}

std::wstring command_line(const std::vector<std::string>& argv) {
  std::wstring line;
  for (const auto& arg : argv) {
    if (!line.empty()) line.push_back(L' ');
    line += quote(widen(arg));
  }
  return line;
}

/// An owned handle, closed on destruction.
class Handle {
 public:
  Handle() = default;
  explicit Handle(HANDLE h) : h_(h) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  [[nodiscard]] HANDLE get() const { return h_; }
  [[nodiscard]] HANDLE* out() {
    reset();
    return &h_;
  }
  void reset() {
    if (h_ != nullptr && h_ != INVALID_HANDLE_VALUE) ::CloseHandle(h_);
    h_ = nullptr;
  }

 private:
  HANDLE h_ = nullptr;
};

/// An inheritable copy of `h` for the child, or nothing when `h` is unusable.
void inheritable_copy(HANDLE h, Handle& out) {
  if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
  ::DuplicateHandle(::GetCurrentProcess(), h, ::GetCurrentProcess(), out.out(), 0, TRUE,
                    DUPLICATE_SAME_ACCESS);
}

/// Starts `argv` with the given standard handles, passing the child exactly those
/// handles and no others. Returns the process handle, or null with `error` set.
HANDLE spawn(const std::vector<std::string>& argv, HANDLE in, HANDLE out, HANDLE err,
             std::string& error) {
  Handle child_in;
  Handle child_out;
  Handle child_err;
  inheritable_copy(in, child_in);
  inheritable_copy(out, child_out);
  inheritable_copy(err, child_err);

  std::vector<HANDLE> inherit;
  for (const auto* h : {&child_in, &child_out, &child_err}) {
    if (h->get() != nullptr) inherit.push_back(h->get());
  }

  SIZE_T attr_size = 0;
  ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
  std::vector<char> attr_buf(attr_size);
  auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
  if (!::InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
    error = fmt::format("motif: failed to run '{}': {}", argv[0], error_text(::GetLastError()));
    return nullptr;
  }
  if (!inherit.empty()) {
    ::UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(),
                                inherit.size() * sizeof(HANDLE), nullptr, nullptr);
  }

  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = child_in.get();
  si.StartupInfo.hStdOutput = child_out.get();
  si.StartupInfo.hStdError = child_err.get();
  si.lpAttributeList = attrs;

  std::wstring line = command_line(argv);
  PROCESS_INFORMATION pi{};
  const BOOL ok = ::CreateProcessW(nullptr, line.data(), nullptr, nullptr, !inherit.empty(),
                                   EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                   &si.StartupInfo, &pi);
  const DWORD spawn_error = ::GetLastError();
  ::DeleteProcThreadAttributeList(attrs);

  if (!ok) {
    error = fmt::format("motif: failed to run '{}': {}", argv[0], error_text(spawn_error));
    return nullptr;
  }
  ::CloseHandle(pi.hThread);
  return pi.hProcess;
}

int wait_for(HANDLE process) {
  ::WaitForSingleObject(process, INFINITE);
  DWORD code = 1;
  ::GetExitCodeProcess(process, &code);
  ::CloseHandle(process);
  return static_cast<int>(code);
}

void read_all(HANDLE h, std::string& sink) {
  char buf[64 * 1024];
  DWORD n = 0;
  while (::ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0) {
    sink.append(buf, n);
  }
}

/// While a child owns the console, Ctrl+C is its business alone. A handler, unlike
/// SetConsoleCtrlHandler(nullptr, TRUE), is not inherited by the child.
BOOL WINAPI ignore_interrupt(DWORD type) {
  return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ? TRUE : FALSE;
}

}  // namespace

Result run(const std::vector<std::string>& argv) {
  Result r;
  if (argv.empty()) {
    r.err = "motif: internal error: empty argv";
    return r;
  }

  Handle out_read;
  Handle out_write;
  Handle err_read;
  Handle err_write;
  if (!::CreatePipe(out_read.out(), out_write.out(), nullptr, 0) ||
      !::CreatePipe(err_read.out(), err_write.out(), nullptr, 0)) {
    r.err = fmt::format("motif: pipe failed: {}", error_text(::GetLastError()));
    return r;
  }

  HANDLE process =
      spawn(argv, ::GetStdHandle(STD_INPUT_HANDLE), out_write.get(), err_write.get(), r.err);
  if (process == nullptr) return r;

  // Release our write ends, or the reads below never reach the end of the pipe.
  out_write.reset();
  err_write.reset();

  // Drain both pipes at once: a child that fills one while we wait on the other would
  // deadlock, and `git cat-file blob` on a 2 MB score does exactly that.
  std::thread err_reader([&] { read_all(err_read.get(), r.err); });
  read_all(out_read.get(), r.out);
  err_reader.join();

  r.exit_code = wait_for(process);
  return r;
}

Result run_attached(const std::vector<std::string>& argv) {
  Result r;
  if (argv.empty()) {
    r.err = "motif: internal error: empty argv";
    return r;
  }

  std::fflush(stdout);
  std::fflush(stderr);

  HANDLE process = spawn(argv, ::GetStdHandle(STD_INPUT_HANDLE), ::GetStdHandle(STD_OUTPUT_HANDLE),
                         ::GetStdHandle(STD_ERROR_HANDLE), r.err);
  if (process == nullptr) return r;

  ::SetConsoleCtrlHandler(ignore_interrupt, TRUE);
  r.exit_code = wait_for(process);
  ::SetConsoleCtrlHandler(ignore_interrupt, FALSE);
  return r;
}

void exec(const std::vector<std::string>& argv) {
  const auto r = run_attached(argv);
  if (!r.err.empty()) {
    fmt::print(stderr, "{}\n", r.err);
    std::exit(127);
  }
  std::exit(r.exit_code);
}

std::filesystem::path self_executable() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    if (n < buf.size()) {
      buf.resize(n);
      return std::filesystem::path(buf);
    }
    buf.resize(buf.size() * 2);
  }
}

}  // namespace motif::proc
