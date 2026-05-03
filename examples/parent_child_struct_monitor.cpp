// Parent starts child process; child starts a writer thread.
// Child writes telemetry_packet structs to IPC on an interval then exits.
// Parent polls, prints only when payload changes, exits when child exits.
//
// Uses process.hpp for cross-platform process spawning (Linux, macOS, Windows).

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

#include "process.hpp"

namespace {

constexpr const char* kChildFlag = "--pc-struct-child";
constexpr std::size_t kDataCapacity = 32768;
constexpr std::size_t kShmSize = xproc::ipc::shm_size_for_data_capacity(kDataCapacity);

struct telemetry_packet {
  char message[256];
  int a;
  int b;
};

int run_child_writer(const std::string& shm_path) {
  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = xproc::ipc::infer_existing_shm_size;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(telemetry_packet);
  opts.create_if_missing = false;

  xproc::ipc::producer producer(opts);

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

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], kChildFlag) == 0) {
    return run_child_writer(std::string(argv[2]));
  }

#if defined(_WIN32) || defined(_WIN64)
  const auto my_pid = static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
  const auto my_pid = static_cast<std::uint64_t>(::getpid());
#endif
  const std::string path = "/xproc_example_parent_child_struct_" + std::to_string(my_pid);

  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(telemetry_packet);
  opts.create_if_missing = true;
  xproc::ipc::consumer consumer(opts);

  const std::string exe = xproc::examples::process::self_exe();
  auto child = xproc::examples::process::spawn({exe, kChildFlag, path});

  telemetry_packet last{};
  bool has_last = false;

  while (true) {
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

  const int rc = child.wait();
  xproc::shm::shm::unlink(path);

  if (rc != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}
