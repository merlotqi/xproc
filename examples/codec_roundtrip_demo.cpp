// Codec helper example: send_encoded + poll_decoded.
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <xproc/xproc.hpp>

int main() {
  const std::string path = "/xproc_example_codec_roundtrip";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 16384;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  std::atomic<bool> done{false};
  std::thread t([&] {
    while (!done.load(std::memory_order_acquire)) {
      bool got = xproc::ipc::poll_decoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(
          consumer, [&](const std::uint64_t& v) {
            std::cout << "decoded value: " << v << "\n";
            done.store(true, std::memory_order_release);
          });
      if (!got) {
        const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  });

  xproc::ipc::send_encoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(producer, 20260326ull);
  t.join();
  xproc::shm::shm::unlink(path);
  return 0;
}
