// Fused demo: (1) Token handshake in a small SHM block then (2) fixed-slot IPC
// telemetry stream where the parent consumer creates the IPC ring before launching the producer child.
//
// Child argv: --handshake-child "<handshake_shm_path>" <16-hex-token> "<ipc_ring_path>"
// Uses process.hpp for cross-platform process spawning (Linux, macOS, Windows).

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#endif

#include "process.hpp"

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
constexpr std::size_t kIpcDataCapacity = 32768;

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
  const std::uint64_t a = (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
  const std::uint64_t b = (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
  return a ^ b;
}

std::uint64_t current_pid() {
#if defined(_WIN32) || defined(_WIN64)
  return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

int run_child_data_writer(const std::string& ipc_path) {
  xproc::ipc::producer producer =
      xproc::ipc::attach_fixed_channel(ipc_path).open_producer();

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
    xproc::core::shm seg;
    if (!seg.open(handshake_path, kHandshakeShmBytes, xproc::core::shm_open_mode::open)) {
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

    h->b_pid.store(current_pid(), std::memory_order_relaxed);
    h->used.store(1u, std::memory_order_release);
  }

  return run_child_data_writer(ipc_path);
}

void parent_consume_until_child_done(xproc::ipc::consumer& consumer, xproc::examples::process& child) {
  telemetry_packet last{};
  bool has_last = false;

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
    if (child.poll_exit()) {
      break;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 5 && std::strcmp(argv[1], kChildFlag) == 0) {
    const int rc = fused_child_main(argc, argv);
    if (rc != 0) {
      std::cerr << "handshake child failed: code " << rc << "\n";
    }
    return rc;
  }

  const std::string base =
      std::string("/xproc_hfused_") + std::to_string(current_pid()) + "_" + std::to_string(make_token() & 0xffffffu);
  const std::string handshake_path = base + "_h";
  const std::string ipc_path = base + "_ipc";

  xproc::core::shm::unlink(handshake_path);
  xproc::core::shm::unlink(ipc_path);

  const std::uint64_t token = make_token();
  const std::string hex = token_to_hex(token);

  xproc::core::shm parent_hs;
  if (!parent_hs.open(handshake_path, kHandshakeShmBytes, xproc::core::shm_open_mode::open_create)) {
    std::cerr << "parent: handshake shm open_create failed\n";
    return 1;
  }

  auto* h = static_cast<handshake_region*>(parent_hs.addr());
  h->valid.store(0u, std::memory_order_relaxed);
  h->used.store(0u, std::memory_order_relaxed);
  h->b_pid.store(0u, std::memory_order_relaxed);
  h->token.store(token, std::memory_order_relaxed);
  h->valid.store(1u, std::memory_order_release);

  const auto ipc_channel = xproc::ipc::make_fixed_channel(ipc_path, sizeof(telemetry_packet)).create(kIpcDataCapacity);
  xproc::ipc::consumer consumer = ipc_channel.open_consumer();

  const std::string exe = xproc::examples::process::self_exe();
  auto child = xproc::examples::process::spawn({exe, kChildFlag, handshake_path, hex, ipc_path});

  const auto deadline = std::chrono::steady_clock::now() + kParentTimeout;
  bool handshake_ok = false;

  while (std::chrono::steady_clock::now() < deadline) {
    if (h->used.load(std::memory_order_acquire) != 0u) {
      const std::uint64_t reported = h->b_pid.load(std::memory_order_acquire);
      handshake_ok = (reported == child.pid());
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
    child.terminate();
    parent_hs.detach();
    xproc::core::shm::unlink(handshake_path);
    xproc::core::shm::unlink(ipc_path);
    return 1;
  }

  parent_hs.detach();
  xproc::core::shm::unlink(handshake_path);
  std::cout << "handshake ok: pid " << child.pid() << ", telemetry follows (ipc " << ipc_path << ")\n";

  parent_consume_until_child_done(consumer, child);
  const int rc = child.wait();

  xproc::core::shm::unlink(ipc_path);
  if (rc != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}
