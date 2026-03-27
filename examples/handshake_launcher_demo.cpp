// Fused demo: (1) Token handshake in a small SHM block (example.md) then (2) fixed-slot IPC
// telemetry stream (same layout as parent_child_struct_monitor.cpp).
//
// Child argv: --handshake-child "<handshake_shm_path>" <16-hex-token> "<ipc_ring_path>"
// Linux: fork + exec self. Windows: CreateProcess.

#if defined(__linux__)

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

#elif defined(_WIN32) || defined(_WIN64)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

#else

#include <iostream>

int main() {
  std::cout << "handshake_launcher_demo: unsupported platform\n";
  return 0;
}

#endif

#if defined(__linux__) || defined(_WIN32) || defined(_WIN64)

namespace {

constexpr const char* kChildFlag = "--handshake-child";
constexpr std::chrono::milliseconds kPollSleep{20};
constexpr std::chrono::seconds kParentTimeout{10};

struct alignas(64) handshake_region {
  std::atomic<std::uint32_t> valid{};
  std::atomic<std::uint32_t> used{};
  std::atomic<std::uint64_t> token{};
  std::atomic<std::uint64_t> b_pid{};
};

constexpr std::size_t kHandshakeShmBytes = sizeof(handshake_region);
constexpr std::size_t kIpcShmSize = sizeof(xproc::shm::shm_control_block) + 32768;

struct telemetry_packet {
  char message[256];
  int a;
  int b;
};

std::string token_to_hex(std::uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

bool hex_to_token(const char* s, std::uint64_t& out) {
  if (s == nullptr || std::strlen(s) != 16) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long x = std::strtoull(s, &end, 16);
  if (end != s + 16 || errno == ERANGE) {
    return false;
  }
  out = static_cast<std::uint64_t>(x);
  return true;
}

std::uint64_t make_token() {
  std::random_device rd;
  const std::uint64_t a =
      (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
  const std::uint64_t b =
      (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
  return a ^ b;
}

int run_child_data_writer(const std::string& ipc_path) {
  xproc::ipc::transport_options opts;
  opts.path = ipc_path;
  opts.shm_size = kIpcShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(telemetry_packet);
  opts.create_if_missing = false;

  xproc::ipc::producer_channel producer(opts);

  std::thread writer([&] {
    for (int i = 0; i <= 100; ++i) {
      telemetry_packet pkt{};
      std::snprintf(pkt.message, sizeof(pkt.message), "tick-%d", i);
      pkt.a = i;
      pkt.b = i * 2;
      producer.send_fixed(pkt);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
  });

  writer.join();
  return 0;
}

// argv: prog --handshake-child <hpath> <hex> <ipc_path>
int fused_child_main(int argc, char** argv) {
  if (argc < 5 || std::strcmp(argv[1], kChildFlag) != 0) {
    return 2;
  }
  const std::string handshake_path = argv[2];
  std::uint64_t expect_tok = 0;
  if (!hex_to_token(argv[3], expect_tok)) {
    return 3;
  }
  const std::string ipc_path = argv[4];

  {
    xproc::shm::shm seg;
    if (!seg.open(handshake_path, kHandshakeShmBytes, xproc::shm::shm_open_mode::open)) {
      return 4;
    }

    auto* h = static_cast<handshake_region*>(seg.addr());

    if (h->valid.load(std::memory_order_acquire) == 0u) {
      return 5;
    }
    if (h->used.load(std::memory_order_acquire) != 0u) {
      return 6;
    }
    if (h->token.load(std::memory_order_relaxed) != expect_tok) {
      return 7;
    }

#if defined(__linux__)
    const std::uint64_t my_pid = static_cast<std::uint64_t>(::getpid());
#elif defined(_WIN32) || defined(_WIN64)
    const std::uint64_t my_pid = static_cast<std::uint64_t>(::GetCurrentProcessId());
#endif
    h->b_pid.store(my_pid, std::memory_order_relaxed);
    h->used.store(1u, std::memory_order_release);
  }

  return run_child_data_writer(ipc_path);
}

void parent_consume_until_child_done(xproc::ipc::consumer_channel& consumer,
#if defined(__linux__)
                                     pid_t child_pid, int& status_out
#elif defined(_WIN32) || defined(_WIN64)
                                     HANDLE child_process, DWORD& exit_code_out
#endif
) {
  telemetry_packet last{};
  bool has_last = false;
#if defined(__linux__)
  status_out = 0;
#elif defined(_WIN32) || defined(_WIN64)
  exit_code_out = 1;
#endif

  for (;;) {
    consumer.poll([&](void* p, std::uint32_t len) {
      if (len != sizeof(telemetry_packet)) {
        return;
      }
      telemetry_packet cur{};
      std::memcpy(&cur, p, sizeof(cur));
      const bool changed = !has_last || std::memcmp(&cur, &last, sizeof(cur)) != 0;
      if (changed) {
        std::cout << "message=" << cur.message << ", a=" << cur.a << ", b=" << cur.b << "\n";
        last = cur;
        has_last = true;
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
#if defined(__linux__)
    if (waitpid(child_pid, &status_out, WNOHANG) == child_pid) {
      break;
    }
#elif defined(_WIN32) || defined(_WIN64)
    if (::WaitForSingleObject(child_process, 0) == WAIT_OBJECT_0) {
      (void)::GetExitCodeProcess(child_process, &exit_code_out);
      break;
    }
#endif
  }
}

}  // namespace

#if defined(__linux__)

int main(int argc, char** argv) {
  if (argc >= 5 && std::strcmp(argv[1], kChildFlag) == 0) {
    return fused_child_main(argc, argv);
  }

  const std::string base = std::string("/xproc_hfused_") + std::to_string(::getpid()) + "_" +
                           std::to_string(make_token() & 0xffffffu);
  const std::string handshake_path = base + "_h";
  const std::string ipc_path = base + "_ipc";

  xproc::shm::shm::unlink(handshake_path);
  xproc::shm::shm::unlink(ipc_path);

  const std::uint64_t token = make_token();
  const std::string hex = token_to_hex(token);

  xproc::shm::shm parent_hs;
  if (!parent_hs.open(handshake_path, kHandshakeShmBytes, xproc::shm::shm_open_mode::open_create)) {
    std::cerr << "parent: handshake shm open_create failed\n";
    return 1;
  }

  auto* h = static_cast<handshake_region*>(parent_hs.addr());
  h->valid.store(0u, std::memory_order_relaxed);
  h->used.store(0u, std::memory_order_relaxed);
  h->b_pid.store(0u, std::memory_order_relaxed);
  h->token.store(token, std::memory_order_relaxed);
  h->valid.store(1u, std::memory_order_release);

  xproc::ipc::transport_options ipc_opts;
  ipc_opts.path = ipc_path;
  ipc_opts.shm_size = kIpcShmSize;
  ipc_opts.type = xproc::ipc::channel_type::fixed;
  ipc_opts.item_size = sizeof(telemetry_packet);
  ipc_opts.create_if_missing = true;

  xproc::ipc::producer_channel ipc_creator(ipc_opts);
  ipc_opts.create_if_missing = false;
  xproc::ipc::consumer_channel consumer(ipc_opts);

  char exe[4096];
  const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n <= 0) {
    std::cerr << "parent: readlink /proc/self/exe failed\n";
    parent_hs.detach();
    xproc::shm::shm::unlink(handshake_path);
    xproc::shm::shm::unlink(ipc_path);
    return 1;
  }
  exe[n] = '\0';

  const pid_t child = ::fork();
  if (child < 0) {
    std::perror("fork");
    parent_hs.detach();
    xproc::shm::shm::unlink(handshake_path);
    xproc::shm::shm::unlink(ipc_path);
    return 1;
  }

  if (child == 0) {
    ::execl(exe, exe, kChildFlag, handshake_path.c_str(), hex.c_str(), ipc_path.c_str(),
            static_cast<char*>(nullptr));
    std::perror("execl");
    _exit(127);
  }

  const auto deadline = std::chrono::steady_clock::now() + kParentTimeout;
  bool handshake_ok = false;
  std::uint64_t reported = 0;

  while (std::chrono::steady_clock::now() < deadline) {
    if (h->used.load(std::memory_order_acquire) != 0u) {
      reported = h->b_pid.load(std::memory_order_acquire);
      handshake_ok = (reported == static_cast<std::uint64_t>(child));
      break;
    }
    std::this_thread::sleep_for(kPollSleep);
  }

  if (!handshake_ok) {
    if (h->used.load(std::memory_order_acquire) == 0u) {
      std::cerr << "parent: timeout waiting for handshake\n";
    } else {
      std::cerr << "parent: pid mismatch (shm=" << reported << " child=" << child << ")\n";
    }
    ::kill(child, SIGKILL);
    int st = 0;
    (void)::waitpid(child, &st, 0);
    parent_hs.detach();
    xproc::shm::shm::unlink(handshake_path);
    xproc::shm::shm::unlink(ipc_path);
    return 1;
  }

  parent_hs.detach();
  xproc::shm::shm::unlink(handshake_path);
  std::cout << "handshake ok: pid " << child << ", telemetry follows (ipc " << ipc_path << ")\n";

  int status = 0;
  parent_consume_until_child_done(consumer, child, status);

  xproc::shm::shm::unlink(ipc_path);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#elif defined(_WIN32) || defined(_WIN64)

int main(int argc, char** argv) {
  if (argc >= 5 && std::strcmp(argv[1], kChildFlag) == 0) {
    const int rc = fused_child_main(argc, argv);
    if (rc != 0) {
      std::cerr << "handshake child failed: code " << rc << "\n";
    }
    return rc;
  }

  const std::string base = std::string("/xproc_hfused_") + std::to_string(::GetCurrentProcessId()) + "_" +
                           std::to_string(make_token() & 0xffffffu);
  const std::string handshake_path = base + "_h";
  const std::string ipc_path = base + "_ipc";

  xproc::shm::shm::unlink(handshake_path);
  xproc::shm::shm::unlink(ipc_path);

  const std::uint64_t token = make_token();
  const std::string hex = token_to_hex(token);

  xproc::shm::shm parent_hs;
  if (!parent_hs.open(handshake_path, kHandshakeShmBytes, xproc::shm::shm_open_mode::open_create)) {
    std::cerr << "parent: handshake shm open_create failed\n";
    return 1;
  }

  auto* h = static_cast<handshake_region*>(parent_hs.addr());
  h->valid.store(0u, std::memory_order_relaxed);
  h->used.store(0u, std::memory_order_relaxed);
  h->b_pid.store(0u, std::memory_order_relaxed);
  h->token.store(token, std::memory_order_relaxed);
  h->valid.store(1u, std::memory_order_release);

  xproc::ipc::transport_options ipc_opts;
  ipc_opts.path = ipc_path;
  ipc_opts.shm_size = kIpcShmSize;
  ipc_opts.type = xproc::ipc::channel_type::fixed;
  ipc_opts.item_size = sizeof(telemetry_packet);
  ipc_opts.create_if_missing = true;

  xproc::ipc::producer_channel ipc_creator(ipc_opts);
  ipc_opts.create_if_missing = false;
  xproc::ipc::consumer_channel consumer(ipc_opts);

  char exe_path[MAX_PATH];
  if (::GetModuleFileNameA(nullptr, exe_path, MAX_PATH) == 0u) {
    std::cerr << "parent: GetModuleFileNameA failed\n";
    parent_hs.detach();
    xproc::shm::shm::unlink(handshake_path);
    xproc::shm::shm::unlink(ipc_path);
    return 1;
  }

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::string cmdline = std::string("\"") + exe_path + "\" " + kChildFlag + " \"" + handshake_path + "\" " +
                        hex + " \"" + ipc_path + "\"";
  std::vector<char> cmd_mut(cmdline.begin(), cmdline.end());
  cmd_mut.push_back('\0');

  if (!::CreateProcessA(exe_path, cmd_mut.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    std::cerr << "parent: CreateProcessA failed\n";
    parent_hs.detach();
    xproc::shm::shm::unlink(handshake_path);
    xproc::shm::shm::unlink(ipc_path);
    return 1;
  }
  ::CloseHandle(pi.hThread);

  const DWORD expected_pid = pi.dwProcessId;
  const auto deadline = std::chrono::steady_clock::now() + kParentTimeout;
  bool handshake_ok = false;

  while (std::chrono::steady_clock::now() < deadline) {
    if (h->used.load(std::memory_order_acquire) != 0u) {
      const std::uint64_t reported = h->b_pid.load(std::memory_order_acquire);
      handshake_ok = (reported == static_cast<std::uint64_t>(expected_pid));
      break;
    }
    std::this_thread::sleep_for(kPollSleep);
  }

  if (!handshake_ok) {
    if (h->used.load(std::memory_order_acquire) == 0u) {
      std::cerr << "parent: timeout waiting for handshake\n";
    } else {
      std::cerr << "parent: pid mismatch\n";
    }
    ::TerminateProcess(pi.hProcess, 1);
    ::CloseHandle(pi.hProcess);
    parent_hs.detach();
    xproc::shm::shm::unlink(handshake_path);
    xproc::shm::shm::unlink(ipc_path);
    return 1;
  }

  parent_hs.detach();
  xproc::shm::shm::unlink(handshake_path);
  std::cout << "handshake ok: pid " << expected_pid << ", telemetry follows (ipc " << ipc_path << ")\n";

  DWORD exit_code = 1;
  parent_consume_until_child_done(consumer, pi.hProcess, exit_code);
  ::CloseHandle(pi.hProcess);

  xproc::shm::shm::unlink(ipc_path);
  if (exit_code != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#endif

#endif
