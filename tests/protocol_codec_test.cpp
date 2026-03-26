#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

static_assert(xproc::protocol::is_codec_v<xproc::protocol::span_codec<64>>);

// Manual little-endian wire layout (not memcpy of struct): custom serialization example.
struct point_codec {
  struct message_type {
    std::uint32_t x{0};
    std::uint32_t y{0};
  };

  static constexpr std::size_t max_encoded_size() noexcept { return 8; }

  static void write_u32_le(std::byte *d, std::uint32_t v) noexcept {
    auto *p = reinterpret_cast<unsigned char *>(d);
    p[0] = static_cast<unsigned char>(v & 0xffu);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xffu);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xffu);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xffu);
  }

  static std::uint32_t read_u32_le(const std::byte *d) noexcept {
    auto *p = reinterpret_cast<const unsigned char *>(d);
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
  }

  static bool encode(std::byte *dst, std::size_t cap, const message_type &msg, std::size_t &out_len) noexcept {
    if (cap < 8) {
      return false;
    }
    write_u32_le(dst, msg.x);
    write_u32_le(dst + 4, msg.y);
    out_len = 8;
    return true;
  }

  static bool decode(const std::byte *src, std::size_t len, message_type &out) noexcept {
    if (len < 8) {
      return false;
    }
    out.x = read_u32_le(src);
    out.y = read_u32_le(src + 4);
    return true;
  }
};

static void test_template_codecs_varlen_shm() {
  const std::string path = "/xproc_protocol_codec_test";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 65536;
  opts.type = xproc::ipc::channel_type::variable;

  std::atomic<bool> producer_ready{false};
  point_codec::message_type received{};
  std::atomic<bool> got_msg{false};

  std::thread consumer([&] {
    while (!producer_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::consumer);
    while (!got_msg.load(std::memory_order_acquire)) {
      if (xproc::ipc::poll_decoded<point_codec>(ch, [&](const point_codec::message_type &m) {
            received = m;
            got_msg.store(true, std::memory_order_release);
          })) {
        continue;
      }
      std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
    }
  });

  {
    xproc::ipc::ipc_channel prod(opts, xproc::ipc::ipc_endpoint::role::producer);
    producer_ready.store(true, std::memory_order_release);
    point_codec::message_type m{0x11223344u, 0x55667788u};
    xproc::ipc::send_encoded<point_codec>(prod, m);
  }

  consumer.join();
  assert(received.x == 0x11223344u);
  assert(received.y == 0x55667788u);

  xproc::shm::shm::unlink(path);
}

static void test_span_codec_varlen_typed_channels() {
  const std::string path = "/xproc_protocol_span_codec_test";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 8192;
  opts.type = xproc::ipc::channel_type::variable;

  std::atomic<bool> producer_ready{false};
  std::vector<std::byte> received;

  std::thread consumer([&] {
    while (!producer_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    xproc::ipc::consumer_channel ch(opts);
    while (true) {
      if (xproc::ipc::poll_decoded<xproc::protocol::span_codec<128>>(
              ch, [&](const xproc::protocol::span_codec<128>::message_type &m) {
                received.assign(m.data(), m.data() + static_cast<std::ptrdiff_t>(m.size()));
              })) {
        break;
      }
      std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
    }
  });

  const std::byte blob[] = {std::byte{0xab}, std::byte{0xcd}, std::byte{0xef}};
  const auto view = std::basic_string_view<std::byte>(blob, sizeof(blob));

  {
    xproc::ipc::producer_channel prod(opts);
    producer_ready.store(true, std::memory_order_release);
    xproc::ipc::send_encoded<xproc::protocol::span_codec<128>>(prod, view);
  }

  consumer.join();
  assert(received.size() == sizeof(blob));
  assert(std::memcmp(received.data(), blob, sizeof(blob)) == 0);

  xproc::shm::shm::unlink(path);
}

static void test_raw_pod_and_bounded_bytes() {
  const std::string path = "/xproc_protocol_pod_test";
  xproc::shm::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 8192;
  opts.type = xproc::ipc::channel_type::variable;

  std::atomic<bool> producer_ready{false};
  std::uint64_t got = 0;

  std::thread th([&] {
    while (!producer_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::consumer);
    while (true) {
      if (xproc::ipc::poll_decoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(
              ch, [&](const std::uint64_t &v) { got = v; })) {
        break;
      }
      std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
    }
  });

  {
    xproc::ipc::ipc_channel prod(opts, xproc::ipc::ipc_endpoint::role::producer);
    producer_ready.store(true, std::memory_order_release);
    xproc::ipc::send_encoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(prod, std::uint64_t{0xc0dec0dec0deull});
  }
  th.join();
  assert(got == 0xc0dec0dec0deull);

  xproc::shm::shm::unlink(path);
}

static void test_identity_icodec_varlen() {
  const std::string path = "/xproc_protocol_icodec_test";
  xproc::shm::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 4096;
  opts.type = xproc::ipc::channel_type::variable;

  std::atomic<bool> producer_ready{false};
  std::vector<std::uint8_t> got;

  std::thread th([&] {
    while (!producer_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::consumer);
    while (got.empty()) {
      if (ch.poll([&](void *p, std::uint32_t len) {
            got.assign(static_cast<std::uint8_t *>(p), static_cast<std::uint8_t *>(p) + len);
          })) {
        continue;
      }
      std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
    }
  });

  xproc::protocol::identity_byte_codec idc;
  const char *msg = "icodec";
  std::vector<std::byte> scratch;
  {
    xproc::ipc::ipc_channel prod(opts, xproc::ipc::ipc_endpoint::role::producer);
    producer_ready.store(true, std::memory_order_release);
    xproc::ipc::send_encoded(prod, idc, reinterpret_cast<const std::byte *>(msg), std::strlen(msg), scratch);
  }
  th.join();
  assert(got.size() == std::strlen(msg));
  assert(std::memcmp(got.data(), msg, got.size()) == 0);

  xproc::shm::shm::unlink(path);
}

}  // namespace

int main() {
  test_template_codecs_varlen_shm();
  test_span_codec_varlen_typed_channels();
  test_raw_pod_and_bounded_bytes();
  test_identity_icodec_varlen();
  return 0;
}
