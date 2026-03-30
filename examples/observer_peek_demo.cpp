// Observer example: monitor traffic without advancing read_pos.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <xproc/xproc.hpp>

int main() {
  const std::string path = "/xproc_example_observer";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 16384;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  xproc::ipc::observer observer(opts);

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

  xproc::shm::shm::unlink(path);
  return 0;
}
