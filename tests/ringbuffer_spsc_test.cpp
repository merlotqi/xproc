#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <spscring/spscring.hpp>

template <std::size_t N>
struct alignas(spscring::control_block) ring_arena {
  std::array<std::uint8_t, N> bytes{};
};

TEST(RingbufferSpsc, FixedSpsc) {
  // Keep the ring large enough that the producer does not block in reserve() waiting for space.
  // With a small capacity, a full ring can wedge: the producer waits on read_wake_seq while the
  // consumer waits on commit_seq for progress that cannot happen until space is freed.
  constexpr std::uint64_t cap = 65536;
  constexpr std::size_t total = sizeof(spscring::control_block) + static_cast<std::size_t>(cap);
  ring_arena<total> arena{};
  auto* hdr = reinterpret_cast<spscring::control_block*>(arena.bytes.data());
  ASSERT_TRUE(spscring::init_control_block(*hdr, cap, spscring::layout_type::fixed, 8u, 16));

  spscring::fixed_writer w(hdr);
  spscring::fixed_reader r(hdr);

  constexpr int n = 500;
  std::atomic<int> received{0};

  std::thread consumer([&] {
    while (received.load(std::memory_order_relaxed) < n) {
      std::uint32_t out_size = 0;
      const void* slot = r.try_read(&out_size);
      if (slot) {
        EXPECT_EQ(std::memcmp(slot, "0123456789abcdef", 16), 0);
        received.fetch_add(1, std::memory_order_relaxed);
        r.read_advance();
        continue;
      }
      std::uint32_t last = hdr->rb_meta.commit_seq.load(std::memory_order_acquire);
      spscring::atomic_wait(&hdr->rb_meta.commit_seq, last);
    }
  });

  for (int i = 0; i < n; ++i) {
    void* buf = w.try_reserve();
    ASSERT_NE(buf, nullptr);
    std::memcpy(buf, "0123456789abcdef", 16);
    w.commit();
  }

  consumer.join();
  EXPECT_EQ(received.load(), n);
}

TEST(RingbufferSpsc, VarlenSpscWrap) {
  constexpr std::uint64_t cap = 128;
  constexpr std::size_t total = sizeof(spscring::control_block) + static_cast<std::size_t>(cap);
  ring_arena<total> arena{};
  auto* hdr = reinterpret_cast<spscring::control_block*>(arena.bytes.data());
  ASSERT_TRUE(spscring::init_control_block(*hdr, cap, spscring::layout_type::varlen, 8u));

  spscring::varlen_writer w(hdr);
  spscring::varlen_reader rd(hdr);

  const char* a = "hello";
  const char* b = "variable-length";
  std::size_t strlen_a = std::strlen(a);
  std::size_t strlen_b = std::strlen(b);
  auto rr0 = w.try_reserve(static_cast<std::uint32_t>(strlen_a));
  ASSERT_TRUE(rr0);
  std::memcpy(rr0.payload, a, strlen_a);
  w.commit(rr0.position);

  auto rr1 = w.try_reserve(static_cast<std::uint32_t>(strlen_b));
  ASSERT_TRUE(rr1);
  std::memcpy(rr1.payload, b, strlen_b);
  w.commit(rr1.position);

  int msgs = 0;
  while (msgs < 2) {
    if (rd.read([&](std::uint8_t*& ptr, std::uint32_t& len, spscring::message_meta&, std::uint64_t&) {
          if (msgs == 0) {
            EXPECT_EQ(len, strlen_a);
            EXPECT_EQ(std::memcmp(ptr, a, len), 0);
          } else {
            EXPECT_EQ(len, strlen_b);
            EXPECT_EQ(std::memcmp(ptr, b, len), 0);
          }
          ++msgs;
          return true;
        })) {
      continue;
    }
    std::uint32_t last = hdr->rb_meta.commit_seq.load(std::memory_order_acquire);
    spscring::atomic_wait(&hdr->rb_meta.commit_seq, last);
  }
  EXPECT_EQ(msgs, 2);
}
