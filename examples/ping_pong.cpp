// Two-process demo: parent creates shared memory and sends a counter; child receives and exits.
// Uses process.hpp for cross-platform process spawning (Linux, macOS, Windows).

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <xproc/ipc/channel.hpp>
#include <xproc/ipc/endpoint.hpp>
#include <xproc/shm/shm_layout.hpp>
#include <xproc/sync/atomic_wait.hpp>

#include "process.hpp"

namespace {

constexpr const char* kChildFlag = "--ping-pong-child";

int child_main(const char* shm_path) {
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(65536);
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.data_align = 8;
  opts.create_if_missing = false;

  xproc::ipc::consumer ch(opts);
  std::uint32_t last = 0;
  for (int i = 0; i < 100; ++i) {
    bool got = false;
    while (!got) {
      got = ch.poll([&](void* p, std::uint32_t len) {
        (void)len;
        std::uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        if (i == 0) {
          last = v;
        } else {
          if (v != last + 1) {
            return;
          }
          last = v;
        }
      });
      if (!got) {
        std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
      }
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], kChildFlag) == 0) {
    return child_main(argv[2]);
  }

  const char* shm_path = "/xproc_ping_pong_demo";
  if (argc > 1) {
    shm_path = argv[1];
  }

  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(65536);
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.data_align = 8;
  opts.create_if_missing = true;

  xproc::shm::shm::unlink(shm_path);

  const std::string exe = xproc::examples::process::self_exe();
  auto child = xproc::examples::process::spawn({exe, kChildFlag, shm_path});

  {
    xproc::ipc::producer ch(opts);
    for (std::uint32_t i = 0; i < 100; ++i) {
      ch.send_fixed(i);
    }
  }

  const int rc = child.wait();
  xproc::shm::shm::unlink(shm_path);

  if (rc != 0) {
    std::cerr << "child failed\n";
    return 1;
  }
  std::cout << "ping_pong ok\n";
  return 0;
}
