# Subprocess Class Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a cross-platform subprocess class at `include/xproc/platform/subprocess.hpp` with spawn, wait, terminate, signal, I/O pipe redirection, environment, and working directory support.

**Architecture:** Single header-only file implementing `pipe_handle`, `process_options`, and `process` classes. Platform-specific code guarded by `XPROC_PLATFORM_LINUX`/`XPROC_PLATFORM_DARWIN`/`XPROC_PLATFORM_WINDOWS` macros. POSIX implementation uses fork+exec; Windows uses CreateProcessA.

**Tech Stack:** C++17, POSIX APIs (fork/exec/pipe/dup2/kill), Windows API (CreateProcessA/STARTUPINFOA/TerminateProcess).

## Global Constraints

- Header-only: no .cpp files
- Namespace: `xproc::platform`
- Platform macros from `include/xproc/platform/platform.hpp` (`XPROC_PLATFORM_LINUX`, `XPROC_PLATFORM_DARWIN`, `XPROC_PLATFORM_WINDOWS`)
- Tests use Google Test (already in project via FetchContent)
- C++17 standard (`std::pair`, `std::vector`, `std::string`, `std::exchange`)

---

### Task 1: Create subprocess.hpp with pipe_handle (POSIX)

**Files:**
- Create: `include/xproc/platform/subprocess.hpp`
- Test: `tests/subprocess_test.cpp`

**Interfaces:**
- Produces: `xproc::platform::pipe_handle` class with `fd()`, `valid()`, `read()`, `write()`, `close()`, move semantics

- [ ] **Step 1: Write the failing test**

Create `tests/subprocess_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include <xproc/platform/subprocess.hpp>

using namespace xproc::platform;

TEST(Subprocess, PipeHandleDefaultInvalid) {
  pipe_handle ph;
  EXPECT_FALSE(ph.valid());
}

TEST(Subprocess, PipeHandleMoveConstruct) {
  pipe_handle a;
  pipe_handle b(std::move(a));
  EXPECT_FALSE(a.valid());
  EXPECT_FALSE(b.valid());
}

TEST(Subprocess, PipeHandleMoveAssign) {
  pipe_handle a;
  pipe_handle b;
  b = std::move(a);
  EXPECT_FALSE(a.valid());
  EXPECT_FALSE(b.valid());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && cmake .. -DXPROC_BUILD_TESTS=ON && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: Compilation error — `subprocess.hpp` not found

- [ ] **Step 3: Write minimal pipe_handle implementation**

Create `include/xproc/platform/subprocess.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <utility>
#include <xproc/platform/platform.hpp>

#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
#include <unistd.h>
#elif defined(XPROC_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace xproc::platform {

class pipe_handle {
 public:
  pipe_handle() = default;

  ~pipe_handle() { close(); }

  pipe_handle(pipe_handle&& other) noexcept
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
      : fd_(other.fd_) {
    other.fd_ = -1;
  }
#elif defined(XPROC_PLATFORM_WINDOWS)
      : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }
#endif

  pipe_handle& operator=(pipe_handle&& other) noexcept {
    if (this != &other) {
      close();
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
      fd_ = other.fd_;
      other.fd_ = -1;
#elif defined(XPROC_PLATFORM_WINDOWS)
      handle_ = other.handle_;
      other.handle_ = INVALID_HANDLE_VALUE;
#endif
    }
    return *this;
  }

  pipe_handle(const pipe_handle&) = delete;
  pipe_handle& operator=(const pipe_handle&) = delete;

  bool valid() const noexcept {
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    return fd_ >= 0;
#elif defined(XPROC_PLATFORM_WINDOWS)
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
#else
    return false;
#endif
  }

#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
  int fd() const noexcept { return fd_; }
#endif

#if defined(XPROC_PLATFORM_WINDOWS)
  void* native_handle() const noexcept { return handle_; }
#endif

  std::size_t write(const void* data, std::size_t len) {
    if (!valid() || data == nullptr || len == 0) return 0;
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    auto n = ::write(fd_, data, len);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
#elif defined(XPROC_PLATFORM_WINDOWS)
    DWORD written = 0;
    if (::WriteFile(handle_, data, static_cast<DWORD>(len), &written, nullptr)) {
      return static_cast<std::size_t>(written);
    }
    return 0;
#else
    return 0;
#endif
  }

  std::size_t read(void* buf, std::size_t len) {
    if (!valid() || buf == nullptr || len == 0) return 0;
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    auto n = ::read(fd_, buf, len);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
#elif defined(XPROC_PLATFORM_WINDOWS)
    DWORD bytes_read = 0;
    if (::ReadFile(handle_, buf, static_cast<DWORD>(len), &bytes_read, nullptr)) {
      return static_cast<std::size_t>(bytes_read);
    }
    return 0;
#else
    return 0;
#endif
  }

  void close() noexcept {
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
#elif defined(XPROC_PLATFORM_WINDOWS)
    if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
      ::CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
#endif
  }

 private:
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
  int fd_{-1};
#elif defined(XPROC_PLATFORM_WINDOWS)
  void* handle_{INVALID_HANDLE_VALUE};
#endif

  friend class process;
};

}  // namespace xproc::platform
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: All3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/xproc/platform/subprocess.hpp tests/subprocess_test.cpp
git commit -m "feat: add pipe_handle class in subprocess.hpp"
```

---

### Task 2: Add process_options struct

**Files:**
- Modify: `include/xproc/platform/subprocess.hpp`
- Modify: `tests/subprocess_test.cpp`

**Interfaces:**
- Produces: `xproc::platform::stdio_mode` enum, `xproc::platform::process_options` struct

- [ ] **Step 1: Write the failing test**

Append to `tests/subprocess_test.cpp`:

```cpp
TEST(Subprocess, ProcessOptionsDefaults) {
  process_options opts;
  EXPECT_TRUE(opts.env.empty());
  EXPECT_TRUE(opts.cwd.empty());
  EXPECT_EQ(opts.stdin_mode, stdio_mode::inherit);
  EXPECT_EQ(opts.stdout_mode, stdio_mode::inherit);
  EXPECT_EQ(opts.stderr_mode, stdio_mode::inherit);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: Compilation error — `process_options` not defined

- [ ] **Step 3: Add stdio_mode and process_options to subprocess.hpp**

Add after `pipe_handle` class, before the closing `}  // namespace xproc::platform`:

```cpp
enum class stdio_mode : std::uint8_t {
  inherit,   // Inherit parent's fd (default)
  pipe,      // Create pipe; parent can read/write
  null_dev,  // Redirect to /dev/null or NUL
};

struct process_options {
  // If non-empty, replaces the entire environment (key=value pairs).
  // If empty, inherits the parent's environment.
  std::vector<std::pair<std::string, std::string>> env;

  // If non-empty, sets the child's working directory.
  std::string cwd;

  stdio_mode stdin_mode  = stdio_mode::inherit;
  stdio_mode stdout_mode = stdio_mode::inherit;
  stdio_mode stderr_mode = stdio_mode::inherit;
};
```

Add `#include <string>` and `#include <vector>` to the top includes.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: All4 tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/xproc/platform/subprocess.hpp tests/subprocess_test.cpp
git commit -m "feat: add process_options and stdio_mode to subprocess.hpp"
```

---

### Task 3: Add process class — basic spawn/wait/terminate (POSIX)

**Files:**
- Modify: `include/xproc/platform/subprocess.hpp`
- Modify: `tests/subprocess_test.cpp`

**Interfaces:**
- Produces: `xproc::platform::process` with `spawn()`, `pid()`, `valid()`, `finished()`, `poll_exit()`, `wait()`, `terminate()`, `self_exe()`

- [ ] **Step 1: Write the failing test**

Append to `tests/subprocess_test.cpp`:

```cpp
TEST(Subprocess, SpawnInvalidArgv) {
  EXPECT_THROW(process::spawn({}), std::invalid_argument);
}

TEST(Subprocess, SpawnAndSelfExe) {
  std::string exe = process::self_exe();
  EXPECT_FALSE(exe.empty());
}

TEST(Subprocess, SpawnWaitExitCode) {
  auto p = process::spawn({"true"});
  EXPECT_TRUE(p.valid());
  EXPECT_FALSE(p.finished());
  int code = p.wait();
  EXPECT_EQ(code, 0);
  EXPECT_TRUE(p.finished());
}

TEST(Subprocess, SpawnWaitExitCodeNonZero) {
  auto p = process::spawn({"false"});
  int code = p.wait();
  EXPECT_NE(code, 0);
}

TEST(Subprocess, PollExit) {
  auto p = process::spawn({"true"});
  // waitpid with WNOHANG may not have results immediately
  p.wait();
  EXPECT_TRUE(p.finished());
}

TEST(Subprocess, Terminate) {
  auto p = process::spawn({"sleep", "60"});
  EXPECT_TRUE(p.valid());
  p.terminate();
  EXPECT_TRUE(p.finished());
}

TEST(TestProcess, MoveConstruct) {
  auto p = process::spawn({"true"});
  EXPECT_TRUE(p.valid());
  process q(std::move(p));
  EXPECT_FALSE(p.valid());
  EXPECT_TRUE(q.valid());
  q.wait();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: Compilation error — `process` not defined

- [ ] **Step 3: Add process class (POSIX only)**

Add before `process_options` in subprocess.hpp. Include `<stdexcept>`, `<string>`, `<vector>`, `<cstring>`. Add `#include <sys/wait.h>` for POSIX.

```cpp
class process {
 public:
  process() = default;
  process(const process&) = delete;
  process& operator=(const process&) = delete;

  process(process&& other) noexcept { move_from(std::move(other)); }

  process& operator=(process&& other) noexcept {
    if (this != &other) {
      reset();
      move_from(std::move(other));
    }
    return *this;
  }

  ~process() { reset(); }

  static std::string self_exe() {
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
#elif defined(XPROC_PLATFORM_WINDOWS)
    char buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
      throw std::runtime_error("GetModuleFileNameA failed");
    }
    return std::string(buf, n);
#else
    throw std::runtime_error("self_exe: unsupported platform");
#endif
  }

  static process spawn(const std::vector<std::string>& argv) {
    return spawn(argv, process_options{});
  }

  static process spawn(const std::vector<std::string>& argv, const process_options& opts);

  std::uint64_t pid() const noexcept {
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    return pid_ > 0 ? static_cast<std::uint64_t>(pid_) : 0u;
#elif defined(XPROC_PLATFORM_WINDOWS)
    return process_info_.dwProcessId != 0 ? static_cast<std::uint64_t>(process_info_.dwProcessId) : 0u;
#else
    return 0u;
#endif
  }

  bool valid() const noexcept {
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    return pid_ > 0;
#elif defined(XPROC_PLATFORM_WINDOWS)
    return process_info_.hProcess != nullptr;
#else
    return false;
#endif
  }

  bool finished() const noexcept { return finished_; }

  bool poll_exit() {
    if (!valid() || finished_) {
      return finished_;
    }
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    const pid_t rc = ::waitpid(pid_, &raw_status_, WNOHANG);
    if (rc == pid_) {
      finished_ = true;
      return true;
    }
    return false;
#elif defined(XPROC_PLATFORM_WINDOWS)
    if (::WaitForSingleObject(process_info_.hProcess, 0) == WAIT_OBJECT_0) {
      finished_ = true;
      (void)::GetExitCodeProcess(process_info_.hProcess, &exit_code_);
      return true;
    }
    return false;
#else
    return false;
#endif
  }

  void terminate() {
    if (!valid() || finished_) {
      return;
    }
    if (poll_exit()) {
      return;
    }
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    ::kill(pid_, SIGKILL);
    (void)::waitpid(pid_, &raw_status_, 0);
    finished_ = true;
#elif defined(XPROC_PLATFORM_WINDOWS)
    (void)::TerminateProcess(process_info_.hProcess, 1);
    (void)::WaitForSingleObject(process_info_.hProcess, INFINITE);
    (void)::GetExitCodeProcess(process_info_.hProcess, &exit_code_);
    finished_ = true;
#endif
  }

  void send_signal(int sig);

  int wait() {
    if (!valid()) {
      return 1;
    }
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    if (!finished_) {
      (void)::waitpid(pid_, &raw_status_, 0);
      finished_ = true;
    }
    if (!WIFEXITED(raw_status_)) {
      return 1;
    }
    return WEXITSTATUS(raw_status_);
#elif defined(XPROC_PLATFORM_WINDOWS)
    if (!finished_) {
      (void)::WaitForSingleObject(process_info_.hProcess, INFINITE);
      (void)::GetExitCodeProcess(process_info_.hProcess, &exit_code_);
      finished_ = true;
    }
    return static_cast<int>(exit_code_);
#else
    return 1;
#endif
  }

  pipe_handle& stdin_pipe() noexcept { return stdin_pipe_; }
  pipe_handle& stdout_pipe() noexcept { return stdout_pipe_; }
  pipe_handle& stderr_pipe() noexcept { return stderr_pipe_; }

 private:
  void move_from(process&& other) noexcept {
    finished_ = std::exchange(other.finished_, false);
    stdin_pipe_ = std::move(other.stdin_pipe_);
    stdout_pipe_ = std::move(other.stdout_pipe_);
    stderr_pipe_ = std::move(other.stderr_pipe_);
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    pid_ = std::exchange(other.pid_, -1);
    raw_status_ = std::exchange(other.raw_status_, 0);
#elif defined(XPROC_PLATFORM_WINDOWS)
    process_info_ = other.process_info_;
    other.process_info_ = PROCESS_INFORMATION{};
    exit_code_ = std::exchange(other.exit_code_, 1);
#endif
  }

  void reset() noexcept {
    if (valid() && !finished_) {
      terminate();
    }
    stdin_pipe_.close();
    stdout_pipe_.close();
    stderr_pipe_.close();
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
    pid_ = -1;
    raw_status_ = 0;
#elif defined(XPROC_PLATFORM_WINDOWS)
    if (process_info_.hThread != nullptr) {
      ::CloseHandle(process_info_.hThread);
      process_info_.hThread = nullptr;
    }
    if (process_info_.hProcess != nullptr) {
      ::CloseHandle(process_info_.hProcess);
      process_info_.hProcess = nullptr;
    }
    exit_code_ = 1;
#endif
    finished_ = false;
  }

  bool finished_{false};
  pipe_handle stdin_pipe_;
  pipe_handle stdout_pipe_;
  pipe_handle stderr_pipe_;

#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
  pid_t pid_{-1};
  int raw_status_{0};
#elif defined(XPROC_PLATFORM_WINDOWS)
  PROCESS_INFORMATION process_info_{};
  DWORD exit_code_{1};
#endif
};
```

- [ ] **Step 4: Add POSIX spawn() implementation**

Add after the class definition, before closing namespace brace. Add `#include <csignal>` for SIGKILL/SIGTERM:

```cpp
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
inline process process::spawn(const std::vector<std::string>& argv, const process_options& opts) {
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

    // Redirect stdin
    if (opts.stdin_mode == stdio_mode::pipe) {
      ::close(stdin_fds[1]);  // Close write end
      ::dup2(stdin_fds[0], STDIN_FILENO);
      ::close(stdin_fds[0]);
    } else if (opts.stdin_mode == stdio_mode::null_dev) {
      ::dup2(null_fd, STDIN_FILENO);
    }

    // Redirect stdout
    if (opts.stdout_mode == stdio_mode::pipe) {
      ::close(stdout_fds[0]);  // Close read end
      ::dup2(stdout_fds[1], STDOUT_FILENO);
      ::close(stdout_fds[1]);
    } else if (opts.stdout_mode == stdio_mode::null_dev) {
      ::dup2(null_fd, STDOUT_FILENO);
    }

    // Redirect stderr
    if (opts.stderr_mode == stdio_mode::pipe) {
      ::close(stderr_fds[0]);  // Close read end
      ::dup2(stderr_fds[1], STDERR_FILENO);
      ::close(stderr_fds[1]);
    } else if (opts.stderr_mode == stdio_mode::null_dev) {
      ::dup2(null_fd, STDERR_FILENO);
    }

    if (null_fd >= 0) ::close(null_fd);

    // Set working directory
    if (!opts.cwd.empty()) {
      if (::chdir(opts.cwd.c_str()) != 0) {
        _exit(127);
      }
    }

    // Set environment
    if (!opts.env.empty()) {
      // Clear existing environment
      extern char** environ;
      environ = nullptr;
      // Build new environment
      static thread_local std::vector<std::string> env_strings;
      static thread_local std::vector<char*> env_ptrs;
      env_strings.clear();
      env_ptrs.clear();
      for (const auto& [key, val] : opts.env) {
        env_strings.push_back(key + "=" + val);
        env_ptrs.push_back(const_cast<char*>(env_strings.back().c_str()));
      }
      env_ptrs.push_back(nullptr);
      environ = env_ptrs.data();
    }

    // Exec
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

  // Close child ends of pipes, keep parent ends
  if (opts.stdin_mode == stdio_mode::pipe) {
    ::close(stdin_fds[0]);  // Close read end
    child.stdin_pipe_.fd_ = stdin_fds[1];
  }
  if (opts.stdout_mode == stdio_mode::pipe) {
    ::close(stdout_fds[1]);  // Close write end
    child.stdout_pipe_.fd_ = stdout_fds[0];
  }
  if (opts.stderr_mode == stdio_mode::pipe) {
    ::close(stderr_fds[1]);  // Close write end
    child.stderr_pipe_.fd_ = stderr_fds[0];
  }
  if (null_fd >= 0) ::close(null_fd);

  return child;
}

inline void process::send_signal(int sig) {
  if (!valid() || finished_) return;
  ::kill(pid_, sig);
}
#endif
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd build && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: All7+ tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/xproc/platform/subprocess.hpp tests/subprocess_test.cpp
git commit -m "feat: add process class with spawn/wait/terminate/send_signal"
```

---

### Task 4: Add I/O pipe redirection tests

**Files:**
- Modify: `tests/subprocess_test.cpp`

**Interfaces:**
- Consumes: `process::spawn(argv, opts)`, `pipe_handle::read()`, `pipe_handle::write()`

- [ ] **Step 1: Write the pipe tests**

Append to `tests/subprocess_test.cpp`:

```cpp
TEST(Subprocess, StdoutPipe) {
  process_options opts;
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"echo", "hello"}, opts);
  EXPECT_TRUE(p.stdout_pipe().valid());

  char buf[64] = {};
  std::size_t n = p.stdout_pipe().read(buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  std::string output(buf, n);
  // echo outputs "hello\n"
  EXPECT_NE(output.find("hello"), std::string::npos);

  p.wait();
}

TEST(Subprocess, StdinPipe) {
  process_options opts;
  opts.stdin_mode = stdio_mode::pipe;
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"cat"}, opts);
  EXPECT_TRUE(p.stdin_pipe().valid());
  EXPECT_TRUE(p.stdout_pipe().valid());

  const char* msg = "test-input";
  p.stdin_pipe().write(msg, std::strlen(msg));
  p.stdin_pipe().close();

  char buf[64] = {};
  std::size_t n = p.stdout_pipe().read(buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  std::string output(buf, n);
  EXPECT_NE(output.find("test-input"), std::string::npos);

  p.wait();
}

TEST(Subprocess, NullDevRedirect) {
  process_options opts;
  opts.stdout_mode = stdio_mode::null_dev;
  auto p = process::spawn({"echo", "should-not-appear"}, opts);
  EXPECT_FALSE(p.stdout_pipe().valid());
  p.wait();
  EXPECT_EQ(p.wait(), 0);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `cd build && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add tests/subprocess_test.cpp
git commit -m "test: add I/O pipe redirection tests for subprocess"
```

---

### Task 5: Add environment and working directory tests

**Files:**
- Modify: `tests/subprocess_test.cpp`

- [ ] **Step 1: Write the env/cwd tests**

Append to `tests/subprocess_test.cpp`:

```cpp
TEST(Subprocess, EnvironmentVariable) {
  process_options opts;
  opts.env = {{"MY_TEST_VAR", "hello_world"}};
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"env"}, opts);

  char buf[4096] = {};
  std::size_t total = 0;
  while (true) {
    std::size_t n = p.stdout_pipe().read(buf + total, sizeof(buf) - total);
    if (n == 0) break;
    total += n;
  }
  std::string output(buf, total);
  EXPECT_NE(output.find("MY_TEST_VAR=hello_world"), std::string::npos);
  p.wait();
}

TEST(Subprocess, WorkingDirectory) {
  process_options opts;
  opts.cwd = "/tmp";
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"pwd"}, opts);

  char buf[1024] = {};
  std::size_t n = p.stdout_pipe().read(buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  std::string output(buf, n);
  EXPECT_NE(output.find("/tmp"), std::string::npos);
  p.wait();
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `cd build && cmake --build . --target xproc_subprocess_test && ./tests/xproc_subprocess_test`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add tests/subprocess_test.cpp
git commit -m "test: add environment and working directory tests for subprocess"
```

---

### Task 6: Add Windows spawn() implementation

**Files:**
- Modify: `include/xproc/platform/subprocess.hpp`

- [ ] **Step 1: Add Windows spawn implementation**

Add after the POSIX `spawn()` and `send_signal()` implementations, before the closing namespace:

```cpp
#elif defined(XPROC_PLATFORM_WINDOWS)
inline process process::spawn(const std::vector<std::string>& argv, const process_options& opts) {
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
    null_handle = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
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

inline void process::send_signal(int sig) {
  if (!valid() || finished_) return;
  // Windows: try GenerateConsoleCtrlEvent for CTRL_C/CTRL_BREAK, else terminate
  if (sig == 2 /* SIGINT */ || sig == 3 /* SIGQUIT */) {
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, process_info_.dwProcessId);
  } else {
    terminate();
  }
}
#endif
```

- [ ] **Step 2: Verify compilation on Windows (or cross-compile check)**

Note: This task cannot be fully tested on Linux. Verify the code compiles by inspection.

- [ ] **Step 3: Commit**

```bash
git add include/xproc/platform/subprocess.hpp
git commit -m "feat: add Windows implementation for subprocess spawn and send_signal"
```

---

### Task 7: Add subprocess test to CMakeLists.txt

**Files:**
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add test executable**

Add `subprocess_test.cpp` to the common test list in `tests/CMakeLists.txt`:

```cmake
set(XPROC_GTEST_TEST_FILES_COMMON
    api_surface_test.cpp
    message_meta_test.cpp
    ringbuffer_spsc_test.cpp
    layout_validate_test.cpp
    ipc_observer_attach_test.cpp
    ipc_diagnostics_test.cpp
    protocol_codec_test.cpp
    socket_transport_test.cpp
    runtime_allocation_test.cpp
    producer_backpressure_test.cpp
    subprocess_test.cpp          # <-- add this line
)
```

- [ ] **Step 2: Build and run all tests**

Run: `cd build && cmake .. -DXPROC_BUILD_TESTS=ON && cmake --build . && ctest --output-on-failure`
Expected: All tests PASS including new subprocess tests

- [ ] **Step 3: Commit**

```bash
git add tests/CMakeLists.txt
git commit -m "build: add subprocess_test to CMake test list"
```

---

### Task 8: Final integration — update xproc.hpp umbrella header

**Files:**
- Modify: `include/xproc/xproc.hpp`

Note: Per spec, `xproc.hpp` does NOT include `subprocess.hpp` — it's an independent platform utility. This task is a no-op. The subprocess header is available via `#include <xproc/platform/subprocess.hpp>` directly.

- [ ] **Step 1: Verify the header is self-contained**

```bash
echo '#include <xproc/platform/subprocess.hpp>' | g++ -std=c++17 -x c++ -fsyntax-only -I include -
```

Expected: No errors

- [ ] **Step 2: Run full test suite**

Run: `cd build && cmake --build . && ctest --output-on-failure`
Expected: All tests PASS

- [ ] **Step 3: Commit (if any changes needed)**

```bash
git add -A
git commit -m "feat: complete subprocess class implementation"
```
