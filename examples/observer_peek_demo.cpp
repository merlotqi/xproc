// Observer example: monitor traffic without advancing read_pos.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <xproc/xproc.hpp>

int main() {
  const std::string path = "/xproc_example_observer";
  xproc::core::shm::unlink(path);

  const auto channel = xproc::ipc::make_fixed_channel(path, sizeof(std::uint32_t)).create(16384);
  xproc::ipc::producer producer = channel.open_producer();
  xproc::ipc::consumer consumer = channel.open_consumer();
  xproc::ipc::observer observer = channel.open_observer();

  producer.send_fixed<std::uint32_t>(0x42u);

  bool peeked = false;
  while (!peeked) {
    peeked = observer.peek([&](const void* p, std::uint32_t len) {
      if (len != 4u) {
        std::cerr << "observer unexpected len\n";
        return;
      }
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      std::cout << "observer sees: " << v << "\n";
    });
    if (!peeked) {
      const std::uint32_t c = observer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&observer.header()->rb_meta.commit_seq, c);
    }
  }

  bool consumed = false;
  while (!consumed) {
    consumed = consumer.poll([&](void* p, std::uint32_t len) {
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      std::cout << "consumer got: " << v << ", len=" << len << "\n";
    });
    if (!consumed) {
      const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
    }
  }

  const auto snap = observer.snapshot();
  std::cout << "snapshot write_pos=" << snap.write_pos << " read_pos=" << snap.read_pos
            << " attach_count=" << snap.attach_count << "\n";

  xproc::core::shm::unlink(path);
  return 0;
}
