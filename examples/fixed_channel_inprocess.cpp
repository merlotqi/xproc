// Fixed-size channel example (single process, two threads).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

int main() {
  const std::string path = "/xproc_example_fixed_inprocess";
  xproc::shm::shm::unlink(path);

  const auto created = xproc::ipc::make_fixed_channel(path, sizeof(std::uint32_t)).create(16384);
  xproc::ipc::producer producer = created.open_producer();
  xproc::ipc::consumer consumer = xproc::ipc::attach_fixed_channel(path).open_consumer();

  std::atomic<bool> done{false};
  std::thread t([&] {
    std::uint32_t expected = 1;
    std::uint32_t got_count = 0;
    while (!done.load(std::memory_order_acquire)) {
      bool got = consumer.poll([&](void* p, std::uint32_t len) {
        if (len != sizeof(std::uint32_t)) {
          std::cerr << "unexpected payload len: " << len << "\n";
          done.store(true, std::memory_order_release);
          return;
        }
        std::uint32_t v = *static_cast<std::uint32_t*>(p);
        std::cout << "recv: " << v << "\n";
        if (v != expected) {
          std::cerr << "sequence mismatch, expected " << expected << " got " << v << "\n";
          done.store(true, std::memory_order_release);
          return;
        }
        ++expected;
        ++got_count;
        if (got_count == 10) {
          done.store(true, std::memory_order_release);
        }
      });
      if (!got) {
        const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  });

  for (std::uint32_t i = 1; i <= 10; ++i) {
    producer.send_fixed(i);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  t.join();
  xproc::shm::shm::unlink(path);
  return 0;
}
