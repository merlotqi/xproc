# Process Class Design Spec

**Date**: 2026-09-09
**Status**: Approved
**Scope**: `include/xproc/platform/process.hpp` — header-only, cross-platform (Linux / macOS / Windows)

## Goal

Promote the existing `examples/process.hpp` helper into a first-class public API at `include/xproc/platform/process.hpp`, adding I/O pipe redirection, environment variable control, working directory, and signal support.

## API Style

Options struct + `process::spawn()` factory. Consistent with xproc's existing `transport_options` pattern.

## File Layout

| File | Action |
|------|--------|
| `include/xproc/platform/process.hpp` | **Existing** — keep as-is (`current_process_id()`) |
| `include/xproc/platform/subprocess.hpp` | **New** — process class, pipe_handle, process_options. Includes `process.hpp` internally. |
| `examples/process.hpp` | **Keep** for now; examples can migrate later |

## Types

### `xproc::platform::stdio_mode`

```cpp
enum class stdio_mode : std::uint8_t {
  inherit,    // Inherit parent's fd (default)
  pipe,       // Create pipe; parent can read/write
  null_dev,   // Redirect to /dev/null or NUL
};
```

### `xproc::platform::process_options`

```cpp
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

### `xproc::platform::pipe_handle`

RAII wrapper for a pipe endpoint (parent side).

```cpp
class pipe_handle {
public:
  pipe_handle() = default;
  ~pipe_handle();                              // Closes fd/HANDLE if open
  pipe_handle(pipe_handle&&) noexcept;
  pipe_handle& operator=(pipe_handle&&) noexcept;
  pipe_handle(const pipe_handle&) = delete;
  pipe_handle& operator=(const pipe_handle&) = delete;

  int fd() const noexcept;                     // POSIX file descriptor
  void* native_handle() const noexcept;         // Windows HANDLE
  bool valid() const noexcept;

  std::size_t write(const void* data, std::size_t len);  // Writable end only
  std::size_t read(void* buf, std::size_t len);           // Readable end only
  void close();
};
```

### `xproc::platform::process`

```cpp
class process {
public:
  // ---- Factory ----
  static process spawn(const std::vector<std::string>& argv);
  static process spawn(const std::vector<std::string>& argv, const process_options& opts);

  // ---- Static queries ----
  static std::string self_exe();

  // ---- Instance queries ----
  std::uint64_t pid() const noexcept;
  bool valid() const noexcept;
  bool finished() const noexcept;

  // ---- Lifecycle ----
  bool poll_exit();            // Non-blocking check; returns true if just exited
  int wait();                  // Blocking wait; returns exit code
  void terminate();            // SIGKILL / TerminateProcess
  void send_signal(int sig);   // POSIX: kill(); Windows: best-effort (GenerateConsoleCtrlEvent or TerminateProcess)

  // ---- I/O pipes (valid only when corresponding stdio_mode::pipe was set) ----
  pipe_handle& stdin_pipe() noexcept;    // Parent writes -> child stdin
  pipe_handle& stdout_pipe() noexcept;   // Child stdout -> parent reads
  pipe_handle& stderr_pipe() noexcept;   // Child stderr -> parent reads

  // ---- Move-only ----
  process(process&&) noexcept;
  process& operator=(process&&) noexcept;
  ~process();
  process(const process&) = delete;
  process& operator=(const process&) = delete;

private:
  process() = default;

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

## Platform Implementation Notes

### POSIX (Linux / macOS)

- **spawn**: `fork()` + `execvp()`. Pipes created with `pipe()`, child calls `dup2()` to map pipe ends to fds 0/1/2, then closes unused ends before `execvp()`.
- **env**: Before `execvp()`, build a new `char*[]` from `opts.env` and set `environ` (or use `execvpe` on Linux).
- **cwd**: `fork()` child calls `chdir(opts.cwd.c_str())` before `execvp()`.
- **send_signal**: `::kill(pid_, sig)`.
- **null_dev**: Open `/dev/null`, `dup2` to the target fd.

### Windows

- **spawn**: `CreateProcessA()` with `STARTUPINFOA.hStdInput/hStdOutput/hStdError` set to pipe handles or `INVALID_HANDLE_VALUE` for null.
- **env**: Build a double-null-terminated environment block from `opts.env`, pass as `lpEnvironment`.
- **cwd**: Pass `opts.cwd.c_str()` as `lpCurrentDirectory`.
- **send_signal**: For CTRL_C/CTRL_BREAK, use `GenerateConsoleCtrlEvent()`. For other signals, fall back to `TerminateProcess()`.
- **null_dev**: Open `NUL` device, assign to `STARTUPINFOA` handles.

## Error Handling

- `spawn()` throws `std::runtime_error` on OS failure (fork/CreateProcess/pipe creation).
- `spawn()` throws `std::invalid_argument` if `argv` is empty.
- `pipe_handle::read()/write()` return actual bytes transferred (0 on EOF / closed).
- `send_signal()` on Windows for unsupported signals: silently maps to `terminate()`.

## Relationship to Existing Code

- `examples/process.hpp` remains for existing demos; they can migrate to the new API incrementally.
- `xproc::platform::current_process_id()` stays in its own file, unaffected.
- `xproc.hpp` does not include `process.hpp` — it's an independent platform utility.
