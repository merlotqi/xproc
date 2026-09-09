// POSIX (Linux / macOS) implementation of subprocess.

#include <xproc/platform/subprocess.hpp>

#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)

#include <csignal>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>

#if defined(XPROC_PLATFORM_DARWIN)
#include <mach-o/dyld.h>
#endif

extern "C" char** environ;

namespace xproc::platform {

// ---------------------------------------------------------------------------
// pipe_handle
// ---------------------------------------------------------------------------

pipe_handle::~pipe_handle() { close(); }

pipe_handle::pipe_handle(pipe_handle&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

pipe_handle& pipe_handle::operator=(pipe_handle&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

bool pipe_handle::valid() const noexcept { return fd_ >= 0; }

int pipe_handle::fd() const noexcept { return fd_; }

std::size_t pipe_handle::write(const void* data, std::size_t len) {
  if (!valid() || data == nullptr || len == 0) return 0;
  auto n = ::write(fd_, data, len);
  return n > 0 ? static_cast<std::size_t>(n) : 0;
}

std::size_t pipe_handle::read(void* buf, std::size_t len) {
  if (!valid() || buf == nullptr || len == 0) return 0;
  auto n = ::read(fd_, buf, len);
  return n > 0 ? static_cast<std::size_t>(n) : 0;
}

void pipe_handle::close() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
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
#if defined(XPROC_PLATFORM_LINUX)
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) {
    throw std::runtime_error("readlink(/proc/self/exe) failed");
  }
  buf[n] = '\0';
  return buf;
#elif defined(XPROC_PLATFORM_DARWIN)
  uint32_t size = 0;
  ::_NSGetExecutablePath(nullptr, &size);
  std::string buf(size, '\0');
  if (::_NSGetExecutablePath(buf.data(), &size) != 0) {
    throw std::runtime_error("_NSGetExecutablePath failed");
  }
  buf.resize(std::strlen(buf.c_str()));
  return buf;
#endif
}

process process::spawn(const std::vector<std::string>& argv) {
  return spawn(argv, process_options{});
}

process process::spawn(const std::vector<std::string>& argv, const process_options& opts) {
  if (argv.empty()) {
    throw std::invalid_argument("process::spawn requires a non-empty argv");
  }

  // Create pipes for redirected stdio
  int stdin_fds[2] = {-1, -1};
  int stdout_fds[2] = {-1, -1};
  int stderr_fds[2] = {-1, -1};
  int null_fd = -1;

  auto cleanup_pipes = [&]() {
    for (int fd : stdin_fds) { if (fd >= 0) ::close(fd); }
    for (int fd : stdout_fds) { if (fd >= 0) ::close(fd); }
    for (int fd : stderr_fds) { if (fd >= 0) ::close(fd); }
    if (null_fd >= 0) ::close(null_fd);
  };

  if (opts.stdin_mode == stdio_mode::pipe) {
    if (::pipe(stdin_fds) != 0) { cleanup_pipes(); throw std::runtime_error("pipe(stdin) failed"); }
  }
  if (opts.stdout_mode == stdio_mode::pipe) {
    if (::pipe(stdout_fds) != 0) { cleanup_pipes(); throw std::runtime_error("pipe(stdout) failed"); }
  }
  if (opts.stderr_mode == stdio_mode::pipe) {
    if (::pipe(stderr_fds) != 0) { cleanup_pipes(); throw std::runtime_error("pipe(stderr) failed"); }
  }

  const bool need_null = (opts.stdin_mode == stdio_mode::null_dev ||
                          opts.stdout_mode == stdio_mode::null_dev ||
                          opts.stderr_mode == stdio_mode::null_dev);
  if (need_null) {
    null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd < 0) { cleanup_pipes(); throw std::runtime_error("open(/dev/null) failed"); }
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    cleanup_pipes();
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    // ---- Child process ----

    if (opts.stdin_mode == stdio_mode::pipe) {
      ::close(stdin_fds[1]);
      ::dup2(stdin_fds[0], STDIN_FILENO);
      ::close(stdin_fds[0]);
    } else if (opts.stdin_mode == stdio_mode::null_dev) {
      ::dup2(null_fd, STDIN_FILENO);
    }

    if (opts.stdout_mode == stdio_mode::pipe) {
      ::close(stdout_fds[0]);
      ::dup2(stdout_fds[1], STDOUT_FILENO);
      ::close(stdout_fds[1]);
    } else if (opts.stdout_mode == stdio_mode::null_dev) {
      ::dup2(null_fd, STDOUT_FILENO);
    }

    if (opts.stderr_mode == stdio_mode::pipe) {
      ::close(stderr_fds[0]);
      ::dup2(stderr_fds[1], STDERR_FILENO);
      ::close(stderr_fds[1]);
    } else if (opts.stderr_mode == stdio_mode::null_dev) {
      ::dup2(null_fd, STDERR_FILENO);
    }

    if (null_fd >= 0) ::close(null_fd);

    if (!opts.cwd.empty()) {
      if (::chdir(opts.cwd.c_str()) != 0) {
        _exit(127);
      }
    }

    if (!opts.env.empty()) {
      ::environ = nullptr;
      static thread_local std::vector<std::string> env_strings;
      static thread_local std::vector<char*> env_ptrs;
      env_strings.clear();
      env_ptrs.clear();
      for (const auto& [key, val] : opts.env) {
        env_strings.push_back(key + "=" + val);
        env_ptrs.push_back(const_cast<char*>(env_strings.back().c_str()));
      }
      env_ptrs.push_back(nullptr);
      ::environ = env_ptrs.data();
    }

    std::vector<char*> raw_argv;
    raw_argv.reserve(argv.size() + 1);
    for (const std::string& arg : argv) {
      raw_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    raw_argv.push_back(nullptr);
    ::execvp(raw_argv[0], raw_argv.data());
    std::perror("execvp");
    _exit(127);
  }

  // ---- Parent process ----
  process child;
  child.pid_ = pid;

  if (opts.stdin_mode == stdio_mode::pipe) {
    ::close(stdin_fds[0]);
    child.stdin_pipe_.fd_ = stdin_fds[1];
  }
  if (opts.stdout_mode == stdio_mode::pipe) {
    ::close(stdout_fds[1]);
    child.stdout_pipe_.fd_ = stdout_fds[0];
  }
  if (opts.stderr_mode == stdio_mode::pipe) {
    ::close(stderr_fds[1]);
    child.stderr_pipe_.fd_ = stderr_fds[0];
  }
  if (null_fd >= 0) ::close(null_fd);

  return child;
}

std::uint64_t process::pid() const noexcept {
  return pid_ > 0 ? static_cast<std::uint64_t>(pid_) : 0u;
}

bool process::valid() const noexcept { return pid_ > 0; }

bool process::finished() const noexcept { return finished_; }

bool process::poll_exit() {
  if (!valid() || finished_) return finished_;
  const pid_t rc = ::waitpid(pid_, &raw_status_, WNOHANG);
  if (rc == pid_) {
    finished_ = true;
    return true;
  }
  return false;
}

int process::wait() {
  if (!valid()) return 1;
  if (!finished_) {
    (void)::waitpid(pid_, &raw_status_, 0);
    finished_ = true;
  }
  if (!WIFEXITED(raw_status_)) return 1;
  return WEXITSTATUS(raw_status_);
}

void process::terminate() {
  if (!valid() || finished_) return;
  if (poll_exit()) return;
  ::kill(pid_, SIGKILL);
  (void)::waitpid(pid_, &raw_status_, 0);
  finished_ = true;
}

void process::send_signal(int sig) {
  if (!valid() || finished_) return;
  ::kill(pid_, sig);
}

pipe_handle& process::stdin_pipe() noexcept { return stdin_pipe_; }
pipe_handle& process::stdout_pipe() noexcept { return stdout_pipe_; }
pipe_handle& process::stderr_pipe() noexcept { return stderr_pipe_; }

void process::move_from(process&& other) noexcept {
  finished_ = std::exchange(other.finished_, false);
  stdin_pipe_ = std::move(other.stdin_pipe_);
  stdout_pipe_ = std::move(other.stdout_pipe_);
  stderr_pipe_ = std::move(other.stderr_pipe_);
  pid_ = std::exchange(other.pid_, -1);
  raw_status_ = std::exchange(other.raw_status_, 0);
}

void process::reset() noexcept {
  if (valid() && !finished_) {
    terminate();
  }
  stdin_pipe_.close();
  stdout_pipe_.close();
  stderr_pipe_.close();
  pid_ = -1;
  raw_status_ = 0;
  finished_ = false;
}

}  // namespace xproc::platform

#endif  // XPROC_PLATFORM_LINUX || XPROC_PLATFORM_DARWIN
