// Windows implementation of subprocess.

#include <xproc/platform/subprocess.hpp>

#if defined(XPROC_PLATFORM_WINDOWS)

namespace xproc::platform {

// ---------------------------------------------------------------------------
// pipe_handle
// ---------------------------------------------------------------------------

pipe_handle::~pipe_handle() { close(); }

pipe_handle::pipe_handle(pipe_handle&& other) noexcept : handle_(other.handle_) {
  other.handle_ = INVALID_HANDLE_VALUE;
}

pipe_handle& pipe_handle::operator=(pipe_handle&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
  }
  return *this;
}

bool pipe_handle::valid() const noexcept {
  return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
}

void* pipe_handle::native_handle() const noexcept { return handle_; }

std::size_t pipe_handle::write(const void* data, std::size_t len) {
  if (!valid() || data == nullptr || len == 0) return 0;
  DWORD written = 0;
  if (::WriteFile(handle_, data, static_cast<DWORD>(len), &written, nullptr)) {
    return static_cast<std::size_t>(written);
  }
  return 0;
}

std::size_t pipe_handle::read(void* buf, std::size_t len) {
  if (!valid() || buf == nullptr || len == 0) return 0;
  DWORD bytes_read = 0;
  if (::ReadFile(handle_, buf, static_cast<DWORD>(len), &bytes_read, nullptr)) {
    return static_cast<std::size_t>(bytes_read);
  }
  return 0;
}

void pipe_handle::close() noexcept {
  if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
    ::CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
}

// ---------------------------------------------------------------------------
// process
// ---------------------------------------------------------------------------

process::process(process&& other) noexcept { move_from(std::move(other)); }

process& process::operator=(process&& other) noexcept {
  if (this != &other) {
    reset();
    move_from(std::move(other));
  }
  return *this;
}

process::~process() { reset(); }

std::string process::self_exe() {
  char buf[MAX_PATH];
  const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) {
    throw std::runtime_error("GetModuleFileNameA failed");
  }
  return std::string(buf, n);
}

process process::spawn(const std::vector<std::string>& argv) {
  return spawn(argv, process_options{});
}

process process::spawn(const std::vector<std::string>& argv, const process_options& opts) {
  if (argv.empty()) {
    throw std::invalid_argument("process::spawn requires a non-empty argv");
  }

  // Build command line
  std::string cmd;
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (i != 0) cmd.push_back(' ');
    const auto& arg = argv[i];
    if (arg.find_first_of(" \t\"") == std::string::npos) {
      cmd += arg;
    } else {
      cmd.push_back('"');
      unsigned backslashes = 0;
      for (const char ch : arg) {
        if (ch == '\\') { ++backslashes; continue; }
        if (ch == '"') { cmd.append(backslashes * 2 + 1, '\\'); cmd.push_back('"'); backslashes = 0; continue; }
        if (backslashes) { cmd.append(backslashes, '\\'); backslashes = 0; }
        cmd.push_back(ch);
      }
      if (backslashes) cmd.append(backslashes * 2, '\\');
      cmd.push_back('"');
    }
  }
  cmd.push_back('\0');

  // Setup stdio handles
  HANDLE stdin_read = INVALID_HANDLE_VALUE, stdin_write = INVALID_HANDLE_VALUE;
  HANDLE stdout_read = INVALID_HANDLE_VALUE, stdout_write = INVALID_HANDLE_VALUE;
  HANDLE stderr_read = INVALID_HANDLE_VALUE, stderr_write = INVALID_HANDLE_VALUE;
  HANDLE null_handle = INVALID_HANDLE_VALUE;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  auto cleanup = [&]() {
    if (stdin_read != INVALID_HANDLE_VALUE) CloseHandle(stdin_read);
    if (stdin_write != INVALID_HANDLE_VALUE) CloseHandle(stdin_write);
    if (stdout_read != INVALID_HANDLE_VALUE) CloseHandle(stdout_read);
    if (stdout_write != INVALID_HANDLE_VALUE) CloseHandle(stdout_write);
    if (stderr_read != INVALID_HANDLE_VALUE) CloseHandle(stderr_read);
    if (stderr_write != INVALID_HANDLE_VALUE) CloseHandle(stderr_write);
    if (null_handle != INVALID_HANDLE_VALUE) CloseHandle(null_handle);
  };

  if (opts.stdin_mode == stdio_mode::pipe) {
    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) { cleanup(); throw std::runtime_error("CreatePipe(stdin) failed"); }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
  }
  if (opts.stdout_mode == stdio_mode::pipe) {
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) { cleanup(); throw std::runtime_error("CreatePipe(stdout) failed"); }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  }
  if (opts.stderr_mode == stdio_mode::pipe) {
    if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) { cleanup(); throw std::runtime_error("CreatePipe(stderr) failed"); }
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
  }

  const bool need_null = (opts.stdin_mode == stdio_mode::null_dev ||
                          opts.stdout_mode == stdio_mode::null_dev ||
                          opts.stderr_mode == stdio_mode::null_dev);
  if (need_null) {
    null_handle = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_EXISTING, 0, nullptr);
    if (null_handle == INVALID_HANDLE_VALUE) { cleanup(); throw std::runtime_error("CreateFile(NUL) failed"); }
  }

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = (opts.stdin_mode == stdio_mode::pipe) ? stdin_read :
                 (opts.stdin_mode == stdio_mode::null_dev) ? null_handle : GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = (opts.stdout_mode == stdio_mode::pipe) ? stdout_write :
                  (opts.stdout_mode == stdio_mode::null_dev) ? null_handle : GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = (opts.stderr_mode == stdio_mode::pipe) ? stderr_write :
                 (opts.stderr_mode == stdio_mode::null_dev) ? null_handle : GetStdHandle(STD_ERROR_HANDLE);

  // Build environment block
  std::string env_block;
  if (!opts.env.empty()) {
    for (const auto& [key, val] : opts.env) {
      env_block += key;
      env_block.push_back('=');
      env_block += val;
      env_block.push_back('\0');
    }
    env_block.push_back('\0');
  }

  PROCESS_INFORMATION pi{};

  BOOL ok = CreateProcessA(
      nullptr, const_cast<char*>(cmd.c_str()),
      nullptr, nullptr, TRUE, 0,
      opts.env.empty() ? nullptr : const_cast<char*>(env_block.data()),
      opts.cwd.empty() ? nullptr : opts.cwd.c_str(),
      &si, &pi);

  // Close child-side handles
  if (stdin_read != INVALID_HANDLE_VALUE) { CloseHandle(stdin_read); stdin_read = INVALID_HANDLE_VALUE; }
  if (stdout_write != INVALID_HANDLE_VALUE) { CloseHandle(stdout_write); stdout_write = INVALID_HANDLE_VALUE; }
  if (stderr_write != INVALID_HANDLE_VALUE) { CloseHandle(stderr_write); stderr_write = INVALID_HANDLE_VALUE; }
  if (null_handle != INVALID_HANDLE_VALUE) { CloseHandle(null_handle); null_handle = INVALID_HANDLE_VALUE; }

  if (!ok) {
    cleanup();
    throw std::runtime_error("CreateProcessA failed");
  }

  process child;
  child.process_info_ = pi;
  if (opts.stdin_mode == stdio_mode::pipe) {
    child.stdin_pipe_.handle_ = stdin_write;
    stdin_write = INVALID_HANDLE_VALUE;
  }
  if (opts.stdout_mode == stdio_mode::pipe) {
    child.stdout_pipe_.handle_ = stdout_read;
    stdout_read = INVALID_HANDLE_VALUE;
  }
  if (opts.stderr_mode == stdio_mode::pipe) {
    child.stderr_pipe_.handle_ = stderr_read;
    stderr_read = INVALID_HANDLE_VALUE;
  }

  CloseHandle(pi.hThread);
  pi.hThread = nullptr;

  return child;
}

std::uint64_t process::pid() const noexcept {
  return process_info_.dwProcessId != 0 ? static_cast<std::uint64_t>(process_info_.dwProcessId) : 0u;
}

bool process::valid() const noexcept { return process_info_.hProcess != nullptr; }

bool process::finished() const noexcept { return finished_; }

bool process::poll_exit() {
  if (!valid() || finished_) return finished_;
  if (::WaitForSingleObject(process_info_.hProcess, 0) == WAIT_OBJECT_0) {
    finished_ = true;
    (void)::GetExitCodeProcess(process_info_.hProcess, &exit_code_);
    return true;
  }
  return false;
}

int process::wait() {
  if (!valid()) return 1;
  if (!finished_) {
    (void)::WaitForSingleObject(process_info_.hProcess, INFINITE);
    (void)::GetExitCodeProcess(process_info_.hProcess, &exit_code_);
    finished_ = true;
  }
  return static_cast<int>(exit_code_);
}

void process::terminate() {
  if (!valid() || finished_) return;
  if (poll_exit()) return;
  (void)::TerminateProcess(process_info_.hProcess, 1);
  (void)::WaitForSingleObject(process_info_.hProcess, INFINITE);
  (void)::GetExitCodeProcess(process_info_.hProcess, &exit_code_);
  finished_ = true;
}

void process::send_signal(int sig) {
  if (!valid() || finished_) return;
  if (sig == 2 /* SIGINT */ || sig == 3 /* SIGQUIT */) {
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, process_info_.dwProcessId);
  } else {
    terminate();
  }
}

pipe_handle& process::stdin_pipe() noexcept { return stdin_pipe_; }
pipe_handle& process::stdout_pipe() noexcept { return stdout_pipe_; }
pipe_handle& process::stderr_pipe() noexcept { return stderr_pipe_; }

void process::move_from(process&& other) noexcept {
  finished_ = std::exchange(other.finished_, false);
  stdin_pipe_ = std::move(other.stdin_pipe_);
  stdout_pipe_ = std::move(other.stdout_pipe_);
  stderr_pipe_ = std::move(other.stderr_pipe_);
  process_info_ = other.process_info_;
  other.process_info_ = PROCESS_INFORMATION{};
  exit_code_ = std::exchange(other.exit_code_, 1);
}

void process::reset() noexcept {
  if (valid() && !finished_) {
    terminate();
  }
  stdin_pipe_.close();
  stdout_pipe_.close();
  stderr_pipe_.close();
  if (process_info_.hThread != nullptr) {
    ::CloseHandle(process_info_.hThread);
    process_info_.hThread = nullptr;
  }
  if (process_info_.hProcess != nullptr) {
    ::CloseHandle(process_info_.hProcess);
    process_info_.hProcess = nullptr;
  }
  exit_code_ = 1;
  finished_ = false;
}

}  // namespace xproc::platform

#endif  // XPROC_PLATFORM_WINDOWS
