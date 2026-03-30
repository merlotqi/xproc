#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

struct always_fail_encode_codec {
  struct message_type {};
  static constexpr std::size_t max_encoded_size() noexcept { return 4; }
  static bool encode(std::byte*, std::size_t, const message_type&, std::size_t&) noexcept { return false; }
  static bool decode(const std::byte*, std::size_t, message_type&) noexcept { return true; }
};

TEST(ProtocolCodec, CodecExceptionOnEncodeFailure) {
  const std::string path = "/xproc_codec_exc_test";
  xproc::shm::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 4096;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = true;
  bool threw = false;
  try {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    xproc::ipc::send_encoded<always_fail_encode_codec>(prod, always_fail_encode_codec::message_type{});
  } catch (const xproc::ipc::codec_exception& e) {
    threw = true;
    EXPECT_EQ(e.code(), xproc::ipc::codec_error::encode_failed);
    const std::error_code ec = e.ec();
    EXPECT_EQ(ec, xproc::ipc::codec_error::encode_failed);
    EXPECT_EQ(std::string(ec.category().name()), "xproc.codec");
  }
  EXPECT_TRUE(threw);
  xproc::shm::shm::unlink(path);
}

static_assert(xproc::protocol::is_codec_v<xproc::protocol::span_codec<64>>);

// Manual little-endian wire layout (not memcpy of struct): custom serialization example.
struct point_codec {
  struct message_type {
    std::uint32_t x{0};
    std::uint32_t y{0};
  };

  static constexpr std::size_t max_encoded_size() noexcept { return 8; }

  static void write_u32_le(std::byte* d, std::uint32_t v) noexcept {
    auto* p = reinterpret_cast<unsigned char*>(d);
    p[0] = static_cast<unsigned char>(v & 0xffu);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xffu);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xffu);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xffu);
  }

  static std::uint32_t read_u32_le(const std::byte* d) noexcept {
    auto* p = reinterpret_cast<const unsigned char*>(d);
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
  }

  static bool encode(std::byte* dst, std::size_t cap, const message_type& msg, std::size_t& out_len) noexcept {
    if (cap < 8) {
      return false;
    }
    write_u32_le(dst, msg.x);
    write_u32_le(dst + 4, msg.y);
    out_len = 8;
    return true;
  }

  static bool decode(const std::byte* src, std::size_t len, message_type& out) noexcept {
    if (len < 8) {
      return false;
    }
    out.x = read_u32_le(src);
    out.y = read_u32_le(src + 4);
    return true;
  }
};

TEST(ProtocolCodec, TemplateCodecsVarlenShm) {
  const std::string path = "/xproc_protocol_codec_test";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  point_codec::message_type received{};
  std::atomic<bool> got_msg{false};
  std::thread consumer_th;

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    consumer_th = std::thread([&] {
      xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
      consumer_attached_promise.set_value();
      while (!got_msg.load(std::memory_order_acquire)) {
        if (xproc::ipc::poll_decoded<point_codec>(ch, [&](const point_codec::message_type& m) {
              received = m;
              got_msg.store(true, std::memory_order_release);
            })) {
          continue;
        }
        std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    point_codec::message_type m{0x11223344u, 0x55667788u};
    xproc::ipc::send_encoded<point_codec>(prod, m);
  }

  consumer_th.join();
  EXPECT_EQ(received.x, 0x11223344u);
  EXPECT_EQ(received.y, 0x55667788u);

  xproc::shm::shm::unlink(path);
}

TEST(ProtocolCodec, SpanCodecVarlenTypedChannels) {
  const std::string path = "/xproc_protocol_span_codec_test";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 8192;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  std::vector<std::byte> received;
  std::thread consumer_th;

  const std::byte blob[] = {std::byte{0xab}, std::byte{0xcd}, std::byte{0xef}};
  const auto view = std::basic_string_view<std::byte>(blob, sizeof(blob));

  {
    xproc::ipc::producer prod(opts);
    consumer_th = std::thread([&] {
      xproc::ipc::consumer ch(opts);
      consumer_attached_promise.set_value();
      while (true) {
        if (xproc::ipc::poll_decoded<xproc::protocol::span_codec<128>>(
                ch, [&](const xproc::protocol::span_codec<128>::message_type& m) {
                  received.assign(m.data(), m.data() + static_cast<std::ptrdiff_t>(m.size()));
                })) {
          break;
        }
        std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    xproc::ipc::send_encoded<xproc::protocol::span_codec<128>>(prod, view);
  }

  consumer_th.join();
  EXPECT_EQ(received.size(), sizeof(blob));
  EXPECT_EQ(std::memcmp(received.data(), blob, sizeof(blob)), 0);

  xproc::shm::shm::unlink(path);
}

TEST(ProtocolCodec, RawPodAndBoundedBytes) {
  const std::string path = "/xproc_protocol_pod_test";
  xproc::shm::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 8192;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  std::uint64_t got = 0;
  std::thread consumer_th;

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    consumer_th = std::thread([&] {
      xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
      consumer_attached_promise.set_value();
      while (true) {
        if (xproc::ipc::poll_decoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(
                ch, [&](const std::uint64_t& v) { got = v; })) {
          break;
        }
        std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    xproc::ipc::send_encoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(prod, std::uint64_t{0xc0dec0dec0deull});
  }
  consumer_th.join();
  EXPECT_EQ(got, 0xc0dec0dec0deull);

  xproc::shm::shm::unlink(path);
}

TEST(ProtocolCodec, IdentityIcodecVarlen) {
  const std::string path = "/xproc_protocol_icodec_test";
  xproc::shm::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 4096;
  opts.type = xproc::ipc::channel_type::varlen;

  std::promise<void> consumer_attached_promise;
  std::future<void> consumer_attached = consumer_attached_promise.get_future();
  std::vector<std::uint8_t> got;
  std::thread consumer_th;

  xproc::protocol::identity_byte_codec idc;
  const char* msg = "icodec";
  std::vector<std::byte> scratch;
  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    consumer_th = std::thread([&] {
      xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
      consumer_attached_promise.set_value();
      while (got.empty()) {
        if (ch.poll([&](void* p, std::uint32_t len) {
              got.assign(static_cast<std::uint8_t*>(p), static_cast<std::uint8_t*>(p) + len);
            })) {
          continue;
        }
        std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
      }
    });
    consumer_attached.wait();
    xproc::ipc::send_encoded(prod, idc, reinterpret_cast<const std::byte*>(msg), std::strlen(msg), scratch);
  }
  consumer_th.join();
  EXPECT_EQ(got.size(), std::strlen(msg));
  EXPECT_EQ(std::memcmp(got.data(), msg, got.size()), 0);

  xproc::shm::shm::unlink(path);
}

}  // namespace
