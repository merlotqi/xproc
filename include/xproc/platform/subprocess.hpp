#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

// ---------------------------------------------------------------------------
// pipe_handle -- RAII wrapper for a pipe endpoint (parent side)
// ---------------------------------------------------------------------------

class pipe_handle {
 public:
  pipe_handle() = default;
  ~pipe_handle();

  pipe_handle(pipe_handle&& other) noexcept;
  pipe_handle& operator=(pipe_handle&& other) noexcept;
  pipe_handle(const pipe_handle&) = delete;
  pipe_handle& operator=(const pipe_handle&) = delete;

  bool valid() const noexcept;

#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
  int fd() const noexcept;
#endif

#if defined(XPROC_PLATFORM_WINDOWS)
  void* native_handle() const noexcept;
#endif

  std::size_t write(const void* data, std::size_t len);
  std::size_t read(void* buf, std::size_t len);
  void close() noexcept;

 private:
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
  int fd_{-1};
#elif defined(XPROC_PLATFORM_WINDOWS)
  void* handle_{INVALID_HANDLE_VALUE};
#endif

  friend class process;
};

// ---------------------------------------------------------------------------
// stdio_mode
// ---------------------------------------------------------------------------

enum class stdio_mode : std::uint8_t {
  inherit,   // Inherit parent's fd (default)
  pipe,      // Create pipe; parent can read/write
  null_dev,  // Redirect to /dev/null or NUL
};

// ---------------------------------------------------------------------------
// process_options
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// process
// ---------------------------------------------------------------------------

class process {
 public:
  process() = default;
  process(const process&) = delete;
  process& operator=(const process&) = delete;

  process(process&& other) noexcept;
  process& operator=(process&& other) noexcept;
  ~process();

  // ---- Static queries ----
  static std::string self_exe();

  // ---- Factory ----
  static process spawn(const std::vector<std::string>& argv);
  static process spawn(const std::vector<std::string>& argv, const process_options& opts);

  // ---- Instance queries ----
  std::uint64_t pid() const noexcept;
  bool valid() const noexcept;
  bool finished() const noexcept;

  // ---- Lifecycle ----
  bool poll_exit();
  int wait();
  void terminate();
  void send_signal(int sig);

  // ---- I/O pipes ----
  pipe_handle& stdin_pipe() noexcept;
  pipe_handle& stdout_pipe() noexcept;
  pipe_handle& stderr_pipe() noexcept;

 private:
  void move_from(process&& other) noexcept;
  void reset() noexcept;

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

}  // namespace xproc::platform
