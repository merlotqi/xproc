// Parent starts child process; child starts a writer thread.
// Child writes variable-length text messages to IPC on an interval then exits.
// Parent polls, prints only when payload changes, exits when child exits.
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

constexpr const char* kChildFlag = "--pc-varlen-child";
constexpr std::size_t kDataCapacity = 32768;
constexpr std::size_t kShmSize = xproc::ipc::shm_size_for_data_capacity(kDataCapacity);

std::string make_message(int i) {
  const std::size_t burst = 12u + static_cast<std::size_t>((i % 5) * 11);
  return "tick-" + std::to_string(i) + " status=" + ((i % 2) == 0 ? "steady" : "busy") +
         " payload=" + std::string(burst, static_cast<char>('a' + (i % 26)));
}

int run_child_writer(const std::string& shm_path) {
  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = xproc::ipc::infer_existing_shm_size;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = false;

  xproc::ipc::producer producer(opts);

  std::thread writer([&] {
    for (int i = 0; i <= 20; ++i) {
      const std::string msg = make_message(i);
      producer.send_varlen(msg.data(), static_cast<std::uint32_t>(msg.size()));
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
  const std::string path = "/xproc_example_parent_child_varlen_" + std::to_string(my_pid);

  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = true;
  xproc::ipc::consumer consumer(opts);

  const std::string exe = xproc::examples::process::self_exe();
  auto child = xproc::examples::process::spawn({exe, kChildFlag, path});

  std::string last;
  bool has_last = false;

  while (true) {
    consumer.poll([&](void* p, std::uint32_t len) {
      const std::string cur(static_cast<const char*>(p), static_cast<std::size_t>(len));
      if (!has_last || cur != last) {
        std::cout << "message(" << len << " bytes)=" << cur << "\n";
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
