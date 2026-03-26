// Parent starts child process; child writer thread sends a fixed struct every second.
// Parent polls every 500ms and prints only when payload changes.
#if !defined(__linux__)

#include <iostream>

int main() {
  std::cout << "parent_child_struct_monitor: Linux-only example\n";
  return 0;
}

#else

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <xproc/xproc.hpp>

namespace {

struct telemetry_packet {
  char message[256];
  int a;
  int b;
};

constexpr const char* kPath = "/xproc_example_parent_child_struct";
constexpr std::size_t kShmSize = sizeof(xproc::shm::shm_control_block) + 32768;

int run_child_writer() {
  xproc::ipc::transport_options opts;
  opts.path = kPath;
  opts.shm_size = kShmSize;
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

}  // namespace

int main() {
  xproc::shm::shm::unlink(kPath);

  xproc::ipc::transport_options opts;
  opts.path = kPath;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(telemetry_packet);
  opts.create_if_missing = true;

  // Create segment first, then open consumer in non-create mode.
  xproc::ipc::producer_channel creator(opts);
  opts.create_if_missing = false;
  xproc::ipc::consumer_channel consumer(opts);

  pid_t pid = fork();
  if (pid < 0) {
    std::perror("fork");
    xproc::shm::shm::unlink(kPath);
    return 1;
  }
  if (pid == 0) {
    _exit(run_child_writer());
  }

  telemetry_packet last{};
  bool has_last = false;
  int status = 0;

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
    if (waitpid(pid, &status, WNOHANG) == pid) {
      break;
    }
  }

  xproc::shm::shm::unlink(kPath);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#endif
