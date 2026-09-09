// protocol_codec_test.cpp previously tested send_encoded / poll_decoded / IByteCodec helpers.
//
// xproc is now a pure buffer transport layer. Codec abstractions were removed because
// serialisation/deserialisation is a user concern. This file now demonstrates the raw
// channel API that users should adopt.
//
//   • Producer: ch.send_varlen(buf, len) / ch.send_fixed_bytes(buf, len)
//   • Consumer: ch.poll([](const message_meta&, void* p, uint32_t len) { ... })

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>
#include <spscring/atomic_wait.hpp>
#include <xproc/xproc.hpp>

namespace {

// -----------------------------------------------------------------------
//  Send raw bytes via varlen channel, receive via poll.
// -----------------------------------------------------------------------
TEST(ProtocolCodec, RawVarlenShm) {
  const std::string path = "/xproc_raw_varlen_test";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  std::vector<std::byte> received;
  std::thread consumer_th;

  const std::byte payload[] = {std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    consumer_th = std::thread([&] {
      xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
      consumer_attached_promise.set_value();
      while (received.empty()) {
        if (ch.poll([&](const xproc::ipc::message_meta&, void* p, std::uint32_t len) {
              auto* bytes = static_cast<const std::byte*>(p);
              received.assign(bytes, bytes + len);
            })) {
          continue;
        }
        std::uint32_t c = ch.header()->spscring_cb.rb_meta.commit_seq.load(std::memory_order_acquire);
        spscring::atomic_wait(&ch.header()->spscring_cb.rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    prod.send_varlen(payload, static_cast<std::uint32_t>(sizeof(payload)));
  }

  consumer_th.join();
  ASSERT_EQ(received.size(), sizeof(payload));
  EXPECT_EQ(std::memcmp(received.data(), payload, sizeof(payload)), 0);

  xproc::core::shm::unlink(path);
}

// -----------------------------------------------------------------------
//  Send raw bytes via fixed channel, receive via poll.
// -----------------------------------------------------------------------
TEST(ProtocolCodec, RawFixedShm) {
  const std::string path = "/xproc_raw_fixed_test";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 8;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  std::vector<std::byte> received;
  std::thread consumer_th;

  const std::byte payload[] = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                               std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}};

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    consumer_th = std::thread([&] {
      xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
      consumer_attached_promise.set_value();
      while (received.empty()) {
        if (ch.poll([&](const xproc::ipc::message_meta&, void* p, std::uint32_t len) {
              auto* bytes = static_cast<const std::byte*>(p);
              received.assign(bytes, bytes + len);
            })) {
          continue;
        }
        std::uint32_t c = ch.header()->spscring_cb.rb_meta.commit_seq.load(std::memory_order_acquire);
        spscring::atomic_wait(&ch.header()->spscring_cb.rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    prod.send_fixed_bytes(payload, static_cast<std::uint32_t>(sizeof(payload)));
  }

  consumer_th.join();
  ASSERT_EQ(received.size(), sizeof(payload));
  EXPECT_EQ(std::memcmp(received.data(), payload, sizeof(payload)), 0);

  xproc::core::shm::unlink(path);
}

// -----------------------------------------------------------------------
//  Typed channel API (producer / consumer) — varlen.
// -----------------------------------------------------------------------
TEST(ProtocolCodec, RawVarlenTypedChannels) {
  const std::string path = "/xproc_raw_varlen_typed_test";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 8192;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  std::vector<std::byte> received;
  std::thread consumer_th;

  const std::byte blob[] = {std::byte{0xab}, std::byte{0xcd}, std::byte{0xef}};

  {
    xproc::ipc::producer prod(opts);
    consumer_th = std::thread([&] {
      xproc::ipc::consumer ch(opts);
      consumer_attached_promise.set_value();
      while (true) {
        if (ch.poll([&](const xproc::ipc::message_meta&, void* p, std::uint32_t len) {
              auto* bytes = static_cast<const std::byte*>(p);
              received.assign(bytes, bytes + len);
            })) {
          break;
        }
        std::uint32_t c = ch.header()->spscring_cb.rb_meta.commit_seq.load(std::memory_order_acquire);
        spscring::atomic_wait(&ch.header()->spscring_cb.rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    prod.send_varlen(blob, static_cast<std::uint32_t>(sizeof(blob)));
  }

  consumer_th.join();
  ASSERT_EQ(received.size(), sizeof(blob));
  EXPECT_EQ(std::memcmp(received.data(), blob, sizeof(blob)), 0);

  xproc::core::shm::unlink(path);
}

// -----------------------------------------------------------------------
//  User demonstrates inline serialisation (manual little-endian) around
//  raw send/poll — the pattern that replaces send_encoded/poll_decoded.
// -----------------------------------------------------------------------
struct point {
  std::uint32_t x{0};
  std::uint32_t y{0};
};

void write_u32_le(std::byte* d, std::uint32_t v) noexcept {
  auto* p = reinterpret_cast<unsigned char*>(d);
  p[0] = static_cast<unsigned char>(v & 0xffu);
  p[1] = static_cast<unsigned char>((v >> 8) & 0xffu);
  p[2] = static_cast<unsigned char>((v >> 16) & 0xffu);
  p[3] = static_cast<unsigned char>((v >> 24) & 0xffu);
}

std::uint32_t read_u32_le(const std::byte* d) noexcept {
  auto* p = reinterpret_cast<const unsigned char*>(d);
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::array<std::byte, 8> encode_point(const point& p) noexcept {
  std::array<std::byte, 8> wire{};
  write_u32_le(wire.data(), p.x);
  write_u32_le(wire.data() + 4, p.y);
  return wire;
}

point decode_point(const std::byte* data) noexcept {
  point p;
  p.x = read_u32_le(data);
  p.y = read_u32_le(data + 4);
  return p;
}

TEST(ProtocolCodec, UserInlineSerialization) {
  const std::string path = "/xproc_user_serde_test";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  point received{};
  std::atomic<bool> got_msg{false};
  std::thread consumer_th;

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    consumer_th = std::thread([&] {
      xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
      consumer_attached_promise.set_value();
      while (!got_msg.load(std::memory_order_acquire)) {
        if (ch.poll([&](const xproc::ipc::message_meta&, void* p, std::uint32_t len) {
              if (len >= 8) {
                received = decode_point(static_cast<const std::byte*>(p));
                got_msg.store(true, std::memory_order_release);
              }
            })) {
          continue;
        }
        std::uint32_t c = ch.header()->spscring_cb.rb_meta.commit_seq.load(std::memory_order_acquire);
        spscring::atomic_wait(&ch.header()->spscring_cb.rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    auto wire = encode_point(point{0x11223344u, 0x55667788u});
    prod.send_varlen(wire.data(), static_cast<std::uint32_t>(wire.size()));
  }

  consumer_th.join();
  EXPECT_EQ(received.x, 0x11223344u);
  EXPECT_EQ(received.y, 0x55667788u);

  xproc::core::shm::unlink(path);
}

// -----------------------------------------------------------------------
//  trivially_copyable<T> user struct sent via send_fixed_bytes.
// -----------------------------------------------------------------------
struct alignas(uint64_t) TrivialPayload {
  uint64_t a;
  uint64_t b;
};

TEST(ProtocolCodec, TriviallyCopyablePOD) {
  const std::string path = "/xproc_pod_raw_test";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 8192;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  TrivialPayload received{};
  std::atomic<bool> got_msg{false};
  std::thread consumer_th;

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    consumer_th = std::thread([&] {
      xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
      consumer_attached_promise.set_value();
      while (!got_msg.load(std::memory_order_acquire)) {
        if (ch.poll([&](const xproc::ipc::message_meta&, void* p, std::uint32_t len) {
              if (len >= sizeof(TrivialPayload)) {
                std::memcpy(&received, p, sizeof(TrivialPayload));
                got_msg.store(true, std::memory_order_release);
              }
            })) {
          continue;
        }
        std::uint32_t c = ch.header()->spscring_cb.rb_meta.commit_seq.load(std::memory_order_acquire);
        spscring::atomic_wait(&ch.header()->spscring_cb.rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    TrivialPayload p{0xc0dec0dec0deull, 0xdeadbeefcafeull};
    prod.send_varlen(&p, static_cast<std::uint32_t>(sizeof(p)));
  }

  consumer_th.join();
  EXPECT_EQ(received.a, 0xc0dec0dec0deull);
  EXPECT_EQ(received.b, 0xdeadbeefcafeull);

  xproc::core::shm::unlink(path);
}

}  // namespace
