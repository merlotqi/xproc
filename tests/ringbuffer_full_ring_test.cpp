// Linux only: pipe byte synchronizes producer entering third reserve (see tests/CMakeLists.txt).

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <spscring/spscring.hpp>

template <std::size_t N>
struct alignas(spscring::control_block) ring_arena {
  std::array<std::uint8_t, N> bytes{};
};

TEST(RingbufferFullRing, ThirdReserveAfterPipeSync) {
  // With spscring's fixed ring, each slot is exactly fixed_item_size bytes (no per-slot header).
  // Use item_size=8, capacity=16 so the ring holds exactly 2 items, making the third reserve fail.
  constexpr std::uint32_t item = 8;
  constexpr std::uint64_t cap = 16;  // Exactly 2 slots
  constexpr std::size_t total = sizeof(spscring::control_block) + static_cast<std::size_t>(cap);
  ring_arena<total> arena{};
  auto* hdr = reinterpret_cast<spscring::control_block*>(arena.bytes.data());
  ASSERT_TRUE(spscring::init_control_block(*hdr, cap, spscring::layout_type::fixed, 8u, item));

  spscring::fixed_writer w(hdr);
  spscring::fixed_reader r(hdr);

  void* buf0 = w.try_reserve();
  ASSERT_NE(buf0, nullptr);
  std::memcpy(buf0, "aaaaaaaa", item);
  w.commit();

  void* buf1 = w.try_reserve();
  ASSERT_NE(buf1, nullptr);
  std::memcpy(buf1, "bbbbbbbb", item);
  w.commit();

  // Ring is now full. Third try_reserve should fail.
  void* buf2 = w.try_reserve();
  EXPECT_EQ(buf2, nullptr);

  // After reading one item, the third reserve should succeed.
  std::uint32_t out_size = 0;
  const void* slot0 = r.try_read(&out_size);
  EXPECT_NE(slot0, nullptr);
  r.read_advance();

  buf2 = w.try_reserve();
  ASSERT_NE(buf2, nullptr);
  std::memcpy(buf2, "cccccccc", item);
  w.commit();

  const void* slot1 = r.try_read(&out_size);
  EXPECT_NE(slot1, nullptr);
  EXPECT_EQ(std::memcmp(slot1, "bbbbbbbb", item), 0);
  r.read_advance();

  const void* slot2 = r.try_read(&out_size);
  EXPECT_NE(slot2, nullptr);
  EXPECT_EQ(std::memcmp(slot2, "cccccccc", item), 0);
  r.read_advance();
}
