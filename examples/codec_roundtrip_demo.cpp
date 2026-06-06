// Buffer transport example: zero-copy style — user handles serialisation around send/poll.
//
// xproc's sole responsibility is moving bytes between producer and consumer.
// Serialisation/deserialisation (codec) is a user concern, not part of the framework.
//
// This example demonstrates the recommended pattern:
//   1. Serialise struct → bytes in user code.
//   2. Send via producer.send_varlen().
//   3. Receive via consumer.poll() and deserialise in the handler.
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <xproc/xproc.hpp>

int main() {
  const std::string path = "/xproc_example_codec_roundtrip";
  xproc::core::shm::unlink(path);

  const auto channel = xproc::ipc::make_varlen_channel(path).create(16384);
  xproc::ipc::producer producer = channel.open_producer();
  xproc::ipc::consumer consumer = channel.open_consumer();

  std::atomic<bool> done{false};
  std::uint64_t received = 0;

  std::thread t([&] {
    while (!done.load(std::memory_order_acquire)) {
      bool got = consumer.poll([&](const xproc::ipc::message_meta&, void* data, std::uint32_t len) {
        if (len >= sizeof(uint64_t)) {
          // User deserialisation: bytes → struct
          std::memcpy(&received, data, sizeof(uint64_t));
          std::cout << "received value: " << received << "\n";
          done.store(true, std::memory_order_release);
        }
      });
      if (!got) {
        const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  });

  const std::uint64_t value = 20260326ull;
  // User serialisation: struct → bytes (trivially copyable, just memcpy)
  producer.send_varlen(&value, sizeof(value));

  t.join();
  xproc::core::shm::unlink(path);
  return 0;
}
