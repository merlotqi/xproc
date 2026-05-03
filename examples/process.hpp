#pragma once

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#elif defined(_WIN32) || defined(_WIN64)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#endif

namespace xproc {
namespace examples {

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

  // Return the path of the currently running executable.
  static std::string self_exe() {
#if defined(__linux__)
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
      throw std::runtime_error("readlink(/proc/self/exe) failed");
    }
    buf[n] = '\0';
    return buf;
#elif defined(__APPLE__)
    uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (::_NSGetExecutablePath(buf.data(), &size) != 0) {
      throw std::runtime_error("_NSGetExecutablePath failed");
    }
    buf.resize(std::strlen(buf.c_str()));
    return buf;
#elif defined(_WIN32) || defined(_WIN64)
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
    if (argv.empty()) {
      throw std::invalid_argument("process::spawn requires a non-empty argv");
    }

    process child;

#if defined(__linux__) || defined(__APPLE__)
    const pid_t pid = ::fork();
    if (pid < 0) {
      throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
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
    child.pid_ = pid;
#elif defined(_WIN32) || defined(_WIN64)
    std::string mutable_cmd = build_windows_command_line(argv);
    mutable_cmd.push_back('\0');

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);

    if (!::CreateProcessA(
            nullptr,
            mutable_cmd.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &child.process_info_)) {
      throw std::runtime_error("CreateProcessA failed");
    }
#else
    throw std::runtime_error("process::spawn is unsupported on this platform");
#endif

    return child;
  }

  std::uint64_t pid() const noexcept {
#if defined(__linux__) || defined(__APPLE__)
    return pid_ > 0 ? static_cast<std::uint64_t>(pid_) : 0u;
#elif defined(_WIN32) || defined(_WIN64)
    return process_info_.dwProcessId != 0 ? static_cast<std::uint64_t>(process_info_.dwProcessId) : 0u;
#else
    return 0u;
#endif
  }

  bool valid() const noexcept {
#if defined(__linux__) || defined(__APPLE__)
    return pid_ > 0;
#elif defined(_WIN32) || defined(_WIN64)
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

#if defined(__linux__) || defined(__APPLE__)
    const pid_t rc = ::waitpid(pid_, &raw_status_, WNOHANG);
    if (rc == pid_) {
      finished_ = true;
      return true;
    }
    return false;
#elif defined(_WIN32) || defined(_WIN64)
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

#if defined(__linux__) || defined(__APPLE__)
    ::kill(pid_, SIGKILL);
    (void)::waitpid(pid_, &raw_status_, 0);
    finished_ = true;
#elif defined(_WIN32) || defined(_WIN64)
    (void)::TerminateProcess(process_info_.hProcess, 1);
    (void)::WaitForSingleObject(process_info_.hProcess, INFINITE);
    (void)::GetExitCodeProcess(process_info_.hProcess, &exit_code_);
    finished_ = true;
#endif
  }

  int wait() {
    if (!valid()) {
      return 1;
    }

#if defined(__linux__) || defined(__APPLE__)
    if (!finished_) {
      (void)::waitpid(pid_, &raw_status_, 0);
      finished_ = true;
    }
    if (!WIFEXITED(raw_status_)) {
      return 1;
    }
    return WEXITSTATUS(raw_status_);
#elif defined(_WIN32) || defined(_WIN64)
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

 private:
#if defined(_WIN32) || defined(_WIN64)
  static std::string quote_windows_arg(const std::string& arg) {
    if (arg.find_first_of(" \t\"") == std::string::npos) {
      return arg;
    }

    std::string out = "\"";
    unsigned backslashes = 0;
    for (const char ch : arg) {
      if (ch == '\\') {
        backslashes += 1;
        continue;
      }
      if (ch == '"') {
        out.append(backslashes * 2 + 1, '\\');
        out.push_back('"');
        backslashes = 0;
        continue;
      }
      if (backslashes != 0) {
        out.append(backslashes, '\\');
        backslashes = 0;
      }
      out.push_back(ch);
    }
    if (backslashes != 0) {
      out.append(backslashes * 2, '\\');
    }
    out.push_back('"');
    return out;
  }

  static std::string build_windows_command_line(const std::vector<std::string>& argv) {
    std::string out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
      if (i != 0) {
        out.push_back(' ');
      }
      out += quote_windows_arg(argv[i]);
    }
    return out;
  }
#endif

  void move_from(process&& other) noexcept {
    finished_ = std::exchange(other.finished_, false);

#if defined(__linux__) || defined(__APPLE__)
    pid_ = std::exchange(other.pid_, -1);
    raw_status_ = std::exchange(other.raw_status_, 0);
#elif defined(_WIN32) || defined(_WIN64)
    process_info_ = other.process_info_;
    other.process_info_ = PROCESS_INFORMATION{};
    exit_code_ = std::exchange(other.exit_code_, 1);
#endif
  }

  void reset() noexcept {
#if defined(__linux__) || defined(__APPLE__)
    pid_ = -1;
    raw_status_ = 0;
#elif defined(_WIN32) || defined(_WIN64)
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

#if defined(__linux__) || defined(__APPLE__)
  pid_t pid_{-1};
  int raw_status_{0};
#elif defined(_WIN32) || defined(_WIN64)
  PROCESS_INFORMATION process_info_{};
  DWORD exit_code_{1};
#endif
};

}  // namespace examples
}  // namespace xproc
