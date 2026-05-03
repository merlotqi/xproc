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

namespace {

constexpr const char* kChildFlag = "--pc-counter-child";
constexpr std::size_t kDataCapacity = 16384;
constexpr std::size_t kShmSize = xproc::ipc::shm_size_for_data_capacity(kDataCapacity);

int run_child_writer(const std::string& shm_path) {
  xproc::ipc::transport_options child_opts;
  child_opts.path = shm_path;
  child_opts.shm_size = xproc::ipc::infer_existing_shm_size;
  child_opts.type = xproc::ipc::channel_type::fixed;
  child_opts.item_size = sizeof(std::uint32_t);
  child_opts.create_if_missing = false;

  xproc::ipc::producer producer(child_opts);

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

#if defined(_WIN32) || defined(_WIN64)
  const auto my_pid = static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
  const auto my_pid = static_cast<std::uint64_t>(::getpid());
#endif
  const std::string path = "/xproc_example_parent_child_counter_" + std::to_string(my_pid);

  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;
  xproc::ipc::consumer consumer(opts);

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
  xproc::shm::shm::unlink(path);

  if (rc != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}
