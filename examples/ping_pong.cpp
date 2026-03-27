// Two-process demo (Linux): parent creates shared memory and sends a counter; child receives and exits.
#ifdef __linux__

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <xproc/ipc/ipc_channel.hpp>
#include <xproc/ipc/ipc_endpoint.hpp>
#include <xproc/shm/shm_layout.hpp>
#include <xproc/sync/atomic_wait.hpp>

int main(int argc, char** argv) {
  const char* shm_path = "/xproc_ping_pong_demo";
  if (argc > 1) {
    shm_path = argv[1];
  }

  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 65536;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.data_align = 8;
  opts.create_if_missing = true;

  pid_t pid = fork();
  if (pid < 0) {
    std::perror("fork");
    return 1;
  }

  if (pid == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    opts.create_if_missing = false;
    xproc::ipc::consumer_channel ch(opts);
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
              _exit(2);
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

  int st = 0;
  {
    xproc::ipc::producer_channel ch(opts);
    for (std::uint32_t i = 0; i < 100; ++i) {
      ch.send_fixed(i);
    }
    waitpid(pid, &st, 0);
  }

  xproc::shm::shm::unlink(shm_path);

  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    std::cerr << "child failed\n";
    return 1;
  }
  std::cout << "ping_pong ok\n";
  return 0;
}

#else

int main() { return 0; }

#endif
