#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

namespace {

std::string unique_path(const char* name) {
  return std::string("/xproc_producer_backpressure_") + name + "_" +
         std::to_string(xproc::platform::current_process_id());
}

xproc::ipc::transport_options fixed_opts(const std::string& path,
                                         std::uint32_t item_size,
                                         std::size_t capacity) {
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
