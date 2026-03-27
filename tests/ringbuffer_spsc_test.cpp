#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>
#include <xproc/xproc.hpp>

namespace {

void init_header(xproc::shm::shm_control_block &h, std::uint64_t cap, std::uint32_t layout_type,
                 std::uint32_t data_align) {
  using xproc::shm::shm_layout_manager;
  h.magic = shm_layout_manager::EXPECTED_MAGIC;
  h.version_major = shm_layout_manager::VERSION_MAJOR;
  h.version_minor = shm_layout_manager::VERSION_MINOR;
  h.header_size = sizeof(xproc::shm::shm_control_block);
  h.layout_type = layout_type;
  h.rb_meta.write_pos.store(0, std::memory_order_relaxed);
  h.rb_meta.read_pos.store(0, std::memory_order_relaxed);
  h.rb_meta.commit_seq.store(0, std::memory_order_relaxed);
  h.rb_meta.read_wake_seq.store(0, std::memory_order_relaxed);
  h.data_capacity = cap;
  h.data_alignment = data_align ? data_align : 8u;
  h.attach_count.store(0, std::memory_order_relaxed);
  h.is_ready.store(true, std::memory_order_release);
  h.producer_pid.store(0, std::memory_order_relaxed);
}

}  // namespace

template <std::size_t N>
struct alignas(xproc::shm::shm_control_block) ring_arena {
  std::array<std::uint8_t, N> bytes{};
};

TEST(RingbufferSpsc, FixedSpsc) {
  // Keep the ring large enough that the producer does not block in reserve() waiting for space.
  // With a small capacity, a full ring can wedge: the producer waits on read_wake_seq while the
  // consumer waits on commit_seq for progress that cannot happen until space is freed.
  constexpr std::uint64_t cap = 65536;
  constexpr std::size_t total = sizeof(xproc::shm::shm_control_block) + static_cast<std::size_t>(cap);
  ring_arena<total> arena{};
  auto *hdr = reinterpret_cast<xproc::shm::shm_control_block *>(arena.bytes.data());
  new (hdr) xproc::shm::shm_control_block{};
  init_header(*hdr, cap, 0, 8);

  xproc::ringbuffer::fixed_writer w(hdr);
  xproc::ringbuffer::fixed_reader r(hdr);

  constexpr std::uint32_t item = 16;
  constexpr int n = 500;
  std::atomic<int> received{0};

  std::thread consumer([&] {
    while (received.load(std::memory_order_relaxed) < n) {
      if (r.try_read(item, [&](void *p) {
            EXPECT_EQ(std::memcmp(p, "0123456789abcdef", item), 0);
            received.fetch_add(1, std::memory_order_relaxed);
          })) {
        continue;
      }
      std::uint32_t last = hdr->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&hdr->rb_meta.commit_seq, last);
    }
  });

  for (int i = 0; i < n; ++i) {
    std::uint64_t pos = 0;
    void *buf = w.reserve(item, pos);
    std::memcpy(buf, "0123456789abcdef", item);
    w.commit(pos);
  }

  consumer.join();
  EXPECT_EQ(received.load(), n);
}

TEST(RingbufferSpsc, VarlenSpscWrap) {
  constexpr std::uint64_t cap = 128;
  constexpr std::size_t total = sizeof(xproc::shm::shm_control_block) + static_cast<std::size_t>(cap);
  ring_arena<total> arena{};
  auto *hdr = reinterpret_cast<xproc::shm::shm_control_block *>(arena.bytes.data());
  new (hdr) xproc::shm::shm_control_block{};
  init_header(*hdr, cap, 1, 8);

  xproc::ringbuffer::varlen_writer w(hdr);
  xproc::ringbuffer::varlen_reader rd(hdr);

  const char *a = "hello";
  const char *b = "variable-length";
  std::size_t strlen_a = std::strlen(a);
  std::size_t strlen_b = std::strlen(b);
  std::uint64_t p0 = 0;
  void *b0 = w.reserve(static_cast<std::uint32_t>(strlen_a), p0);
  std::memcpy(b0, a, strlen_a);
  w.commit(p0);

  std::uint64_t p1 = 0;
  void *b1 = w.reserve(static_cast<std::uint32_t>(strlen_b), p1);
  std::memcpy(b1, b, strlen_b);
  w.commit(p1);

  int msgs = 0;
  while (msgs < 2) {
    if (rd.try_read([&](void *ptr, std::uint32_t len) {
          if (msgs == 0) {
            EXPECT_EQ(len, strlen_a);
            EXPECT_EQ(std::memcmp(ptr, a, len), 0);
          } else {
            EXPECT_EQ(len, strlen_b);
            EXPECT_EQ(std::memcmp(ptr, b, len), 0);
          }
          ++msgs;
        })) {
      continue;
    }
    std::uint32_t last = hdr->rb_meta.commit_seq.load(std::memory_order_acquire);
    xproc::sync::atomic_wait(&hdr->rb_meta.commit_seq, last);
  }
  EXPECT_EQ(msgs, 2);
}
