#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <xproc/xproc.hpp>

namespace {

std::string unique_path(const char* name) {
  return std::string("/xproc_producer_backpressure_") + name + "_" +
         std::to_string(xproc::platform::current_process_id());
}

xproc::ipc::transport_options fixed_opts(const std::string& path, std::uint32_t item_size, std::size_t capacity) {
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(capacity);
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = item_size;
  opts.create_if_missing = true;
  return opts;
}

xproc::ipc::transport_options varlen_opts(const std::string& path, std::size_t capacity) {
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(capacity);
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = true;
  return opts;
}

}  // namespace

// ---- watermarks ----

TEST(ProducerBackpressure, WatermarksTrackFixedOccupancy) {
  const std::string path = unique_path("watermarks");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 64);

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  EXPECT_EQ(producer.capacity_bytes(), 64u);
  EXPECT_EQ(producer.used_bytes(), 0u);
  EXPECT_EQ(producer.available_bytes(), 64u);
  EXPECT_DOUBLE_EQ(producer.fill_ratio(), 0.0);

  const std::uint64_t value = 0x1122334455667788ull;
  producer.send_fixed(value);

  EXPECT_GT(producer.used_bytes(), 0u);
  EXPECT_LT(producer.available_bytes(), producer.capacity_bytes());
  EXPECT_GT(producer.fill_ratio(), 0.0);

  ASSERT_TRUE(consumer.poll([](void*, std::uint32_t) {}));
  EXPECT_EQ(producer.used_bytes(), 0u);
  EXPECT_EQ(producer.available_bytes(), 64u);

  xproc::core::shm::unlink(path);
}

// ---- fixed try_send / timeout / oversized / stride ----

TEST(ProducerBackpressure, TrySendFixedReportsFullWithoutBlocking) {
  const std::string path = unique_path("fixed_full");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  const std::uint64_t a = 1;
  const std::uint64_t b = 2;
  const std::uint64_t c = 3;

  EXPECT_EQ(producer.try_send_fixed_sized(&a, sizeof(a)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_fixed_sized(&b, sizeof(b)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_fixed_sized(&c, sizeof(c)), xproc::ipc::send_result::full);

  xproc::core::shm::unlink(path);
}

TEST(ProducerBackpressure, FixedSendForTimesOutWhenRingStaysFull) {
  const std::string path = unique_path("fixed_timeout");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  const std::uint64_t a = 1;
  const std::uint64_t b = 2;
  const std::uint64_t c = 3;

  ASSERT_EQ(producer.try_send_fixed_sized(&a, sizeof(a)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_fixed_sized(&b, sizeof(b)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.send_fixed_sized_for(&c, sizeof(c), std::chrono::milliseconds(2)),
            xproc::ipc::send_result::timeout);

  xproc::core::shm::unlink(path);
}

TEST(ProducerBackpressure, FixedOversizedMessageFailsImmediately) {
  const std::string path = unique_path("fixed_oversized");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 64, 32);

  xproc::ipc::producer producer(opts);
  std::uint64_t value = 0;

  EXPECT_EQ(producer.try_send_fixed_sized(&value, sizeof(value)), xproc::ipc::send_result::message_too_large);

  xproc::core::shm::unlink(path);
}

TEST(ProducerBackpressure, SendFixedSizedUsesConfiguredSlotStride) {
  const std::string path = unique_path("fixed_stride");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 16, 48);

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  const std::uint32_t one = 0x11111111u;
  const std::uint32_t two = 0x22222222u;
  ASSERT_EQ(producer.try_send_fixed_sized(&one, sizeof(one)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_fixed_sized(&two, sizeof(two)), xproc::ipc::send_result::ok);

  std::uint32_t seen_one = 0;
  std::uint32_t seen_two = 0;
  ASSERT_TRUE(consumer.poll([&](void* p, std::uint32_t len) {
    EXPECT_EQ(len, 16u);
    std::memcpy(&seen_one, p, sizeof(seen_one));
  }));
  ASSERT_TRUE(consumer.poll([&](void* p, std::uint32_t len) {
    EXPECT_EQ(len, 16u);
    std::memcpy(&seen_two, p, sizeof(seen_two));
  }));

  EXPECT_EQ(seen_one, one);
  EXPECT_EQ(seen_two, two);

  xproc::core::shm::unlink(path);
}

// ---- fixed bytes API variants ----

TEST(ProducerBackpressure, TrySendFixedBytesPadsPayload) {
  const std::string path = unique_path("fixed_bytes");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  const char payload[3] = {'a', 'b', 'c'};
  ASSERT_EQ(producer.try_send_fixed_bytes(payload, sizeof(payload)), xproc::ipc::send_result::ok);

  ASSERT_TRUE(consumer.poll([&](void* p, std::uint32_t len) {
    ASSERT_EQ(len, 8u);
    const auto* bytes = static_cast<const char*>(p);
    EXPECT_EQ(bytes[0], 'a');
    EXPECT_EQ(bytes[1], 'b');
    EXPECT_EQ(bytes[2], 'c');
    EXPECT_EQ(bytes[3], '\0');
    EXPECT_EQ(bytes[7], '\0');
  }));

  xproc::core::shm::unlink(path);
}

TEST(ProducerBackpressure, SendFixedBytesForCanTimeout) {
  const std::string path = unique_path("fixed_bytes_timeout");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  const std::uint64_t value = 7;
  ASSERT_EQ(producer.try_send_fixed_bytes(&value, sizeof(value)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_fixed_bytes(&value, sizeof(value)), xproc::ipc::send_result::ok);

  EXPECT_EQ(producer.send_fixed_bytes_for(&value, sizeof(value), std::chrono::milliseconds(2)),
            xproc::ipc::send_result::timeout);

  xproc::core::shm::unlink(path);
}

// ---- varlen try_send / timeout / oversized ----

TEST(ProducerBackpressure, TrySendVarlenReportsFullWithoutBlocking) {
  const std::string path = unique_path("varlen_full");
  xproc::core::shm::unlink(path);
  auto opts = varlen_opts(path, 32);

  xproc::ipc::producer producer(opts);
  const char payload[8] = {'x', 'p', 'r', 'o', 'c', '1', '2', '3'};

  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::full);

  xproc::core::shm::unlink(path);
}

TEST(ProducerBackpressure, VarlenSendForTimesOutWhenRingStaysFull) {
  const std::string path = unique_path("varlen_timeout");
  xproc::core::shm::unlink(path);
  auto opts = varlen_opts(path, 32);

  xproc::ipc::producer producer(opts);
  const char payload[8] = {'x', 'p', 'r', 'o', 'c', '1', '2', '3'};
  ASSERT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);

  EXPECT_EQ(producer.send_varlen_for(payload, sizeof(payload), std::chrono::milliseconds(2)),
            xproc::ipc::send_result::timeout);

  xproc::core::shm::unlink(path);
}

TEST(ProducerBackpressure, VarlenOversizedMessageFailsImmediately) {
  const std::string path = unique_path("varlen_oversized");
  xproc::core::shm::unlink(path);
  auto opts = varlen_opts(path, 32);

  xproc::ipc::producer producer(opts);
  char payload[64]{};

  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::message_too_large);

  xproc::core::shm::unlink(path);
}

// ---- timeout success (consumer drains during wait) ----

TEST(ProducerBackpressure, FixedSendForSucceedsWhenConsumerDrains) {
  const std::string path = unique_path("fixed_timeout_success");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  const std::uint64_t a = 1;
  const std::uint64_t b = 2;
  const std::uint64_t c = 3;

  ASSERT_EQ(producer.try_send_fixed_sized(&a, sizeof(a)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_fixed_sized(&b, sizeof(b)), xproc::ipc::send_result::ok);

  std::thread drain([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(consumer.poll([](void*, std::uint32_t) {}));
  });

  EXPECT_EQ(producer.send_fixed_sized_for(&c, sizeof(c), std::chrono::milliseconds(100)), xproc::ipc::send_result::ok);
  drain.join();

  xproc::core::shm::unlink(path);
}
