// Parent starts child process; child starts a writer thread.
// Child writes [0..100] to IPC on an interval then exits.
// Parent polls, prints only when value changes, exits when child exits.
//
// Uses process.hpp for cross-platform process spawning (Linux, macOS, Windows).

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

#include "process.hpp"
#include "xproc/platform/process.hpp"

namespace {

constexpr const char* kChildFlag = "--pc-counter-child";
constexpr std::size_t kDataCapacity = 16384;

int run_child_writer(const std::string& shm_path) {
  xproc::ipc::producer producer =
      xproc::ipc::attach_fixed_channel(shm_path).open_producer();

  std::thread writer([&] {
    for (std::uint32_t v = 0; v <= 100; ++v) {
      producer.send_fixed(v);
      std::this_thread::sleep_for(std::chrono::seconds(1));
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

  const auto my_pid = xproc::platform::current_process_id();
  const std::string path = "/xproc_example_parent_child_counter_" + std::to_string(my_pid);

  xproc::core::shm::unlink(path);

  const auto channel = xproc::ipc::make_fixed_channel(path, sizeof(std::uint32_t)).create(kDataCapacity);
  xproc::ipc::consumer consumer = channel.open_consumer();

  const std::string exe = xproc::examples::process::self_exe();
  auto child = xproc::examples::process::spawn({exe, kChildFlag, path});

  bool has_last = false;
  std::uint32_t last_value = 0;

  while (true) {
    const bool got = consumer.poll([&](void* p, std::uint32_t len) {
      if (len != sizeof(std::uint32_t)) {
        return;
      }
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      if (!has_last || v != last_value) {
        std::cout << "value changed: " << v << "\n";
        last_value = v;
        has_last = true;
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (child.poll_exit()) {
      break;
    }
  }

  const int rc = child.wait();
  xproc::core::shm::unlink(path);

  if (rc != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}
