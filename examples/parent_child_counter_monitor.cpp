// Parent starts child process; child starts a writer thread.
// Child writes [0..100] to IPC once per second then exits.
// Parent polls every 500ms, prints only when value changes, exits when child exits.
#if !defined(__linux__)

#include <iostream>

int main() {
  std::cout << "parent_child_counter_monitor: Linux-only example\n";
  return 0;
}

#else

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <xproc/xproc.hpp>

namespace {

int run_child_writer() {
  xproc::ipc::transport_options child_opts;
  child_opts.path = "/xproc_example_parent_child_counter";
  child_opts.shm_size = sizeof(xproc::shm::shm_control_block) + 16384;
  child_opts.type = xproc::ipc::channel_type::fixed;
  child_opts.item_size = sizeof(std::uint32_t);
  child_opts.create_if_missing = false;  // open existing segment created by parent

  xproc::ipc::producer_channel producer(child_opts);

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

int main() {
  const std::string path = "/xproc_example_parent_child_counter";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 16384;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  // Parent must create the segment first. consumer_channel alone only opens existing segments.
  xproc::ipc::producer_channel creator(opts);
  opts.create_if_missing = false;
  xproc::ipc::consumer_channel consumer(opts);

  pid_t pid = fork();
  if (pid < 0) {
    std::perror("fork");
    xproc::shm::shm::unlink(path);
    return 1;
  }

  if (pid == 0) {
    const int rc = run_child_writer();
    _exit(rc);
  }

  bool has_last = false;
  std::uint32_t last_value = 0;
  int status = 0;

  while (true) {
    bool got = consumer.poll([&](void* p, std::uint32_t len) {
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

    if (!got) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } else {
      // Keep the external polling cadence from the requirement.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      break;
    }
  }

  xproc::shm::shm::unlink(path);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#endif
