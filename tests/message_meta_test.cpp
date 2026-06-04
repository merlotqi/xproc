#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

std::string unique_path(const char* name) {
  return std::string("/xproc_message_meta_") + name + "_" + std::to_string(xproc::platform::current_process_id());
}

xproc::ipc::transport_options fixed_opts(const std::string& path, std::uint32_t item_size, std::size_t cap) {
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(cap);
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = item_size;
  opts.create_if_missing = true;
  return opts;
}

xproc::ipc::transport_options varlen_opts(const std::string& path, std::size_t cap) {
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(cap);
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = true;
  return opts;
}

}  // namespace

// ============================================================
// Meta struct unit tests
// ============================================================

TEST(Meta, DefaultValues) {
  xproc::ipc::message_meta meta;
  EXPECT_EQ(meta.user_data, 0u);
  EXPECT_EQ(meta.timestamp_ns, 0u);
  EXPECT_EQ(meta.schema_id, 0u);
  EXPECT_EQ(meta.flags, 0u);
}

// ============================================================
// Fixed channel: meta is delivered in poll callback
// ============================================================

TEST(Meta, FixedSlotContainsMeta) {
  const std::string path = unique_path("fixed_meta");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, sizeof(std::uint32_t), 256);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    const std::uint32_t value = 42;
    producer.send_fixed(value);

    bool got = false;
    ASSERT_TRUE(consumer.poll([&](const xproc::ipc::message_meta& meta, void* p, std::uint32_t len) {
      got = true;
      EXPECT_NE(meta.timestamp_ns, 0u);
      EXPECT_EQ(len, sizeof(std::uint32_t));
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      EXPECT_EQ(v, 42u);
    }));
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// Varlen channel: meta is delivered in poll callback
// ============================================================

TEST(Meta, VarlenSlotContainsMeta) {
  const std::string path = unique_path("varlen_meta");
  xproc::core::shm::unlink(path);
  auto opts = varlen_opts(path, 4096);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    const char payload[] = "hello-meta";
    producer.send_varlen(payload, static_cast<std::uint32_t>(sizeof(payload)));

    bool got = false;
    ASSERT_TRUE(consumer.poll([&](const xproc::ipc::message_meta& meta, void* p, std::uint32_t len) {
      got = true;
      EXPECT_NE(meta.timestamp_ns, 0u);
      EXPECT_EQ(len, sizeof(payload));
      EXPECT_EQ(std::memcmp(p, payload, sizeof(payload)), 0);
    }));
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// user_data round-trip
// ============================================================

TEST(Meta, UserDataRoundtrip) {
  const std::string path = unique_path("user_data");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, sizeof(std::uint32_t), 256);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    xproc::ipc::message_meta send_meta;
    send_meta.user_data = 42;

    const std::uint32_t value = 0xDEAD;
    producer.send_fixed(value, send_meta);

    bool got = false;
    ASSERT_TRUE(consumer.poll([&](const xproc::ipc::message_meta& meta, void*, std::uint32_t) {
      got = true;
      EXPECT_EQ(meta.user_data, 42u);
      EXPECT_NE(meta.timestamp_ns, 0u);
    }));
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// flags round-trip
// ============================================================

TEST(Meta, FlagsRoundtrip) {
  const std::string path = unique_path("flags");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, sizeof(std::uint32_t), 256);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    xproc::ipc::message_meta send_meta;
    send_meta.flags = xproc::ipc::flag_priority_high | xproc::ipc::flag_compressed;

    const std::uint32_t value = 0xBEAD;
    producer.send_fixed(value, send_meta);

    bool got = false;
    ASSERT_TRUE(consumer.poll([&](const xproc::ipc::message_meta& meta, void*, std::uint32_t) {
      got = true;
      EXPECT_EQ(meta.flags, static_cast<std::uint32_t>(xproc::ipc::flag_priority_high | xproc::ipc::flag_compressed));
      EXPECT_NE(meta.timestamp_ns, 0u);
    }));
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// schema_id round-trip
// ============================================================

TEST(Meta, SchemaIdRoundtrip) {
  const std::string path = unique_path("schema_id");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, sizeof(std::uint32_t), 256);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    xproc::ipc::message_meta send_meta;
    send_meta.schema_id = 99;

    const std::uint32_t value = 0xCAFE;
    producer.send_fixed(value, send_meta);

    bool got = false;
    ASSERT_TRUE(consumer.poll([&](const xproc::ipc::message_meta& meta, void*, std::uint32_t) {
      got = true;
      EXPECT_EQ(meta.schema_id, 99u);
      EXPECT_NE(meta.timestamp_ns, 0u);
    }));
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// Default meta: timestamp is auto-filled by the writer
// ============================================================

TEST(Meta, DefaultMetaHasTimestamp) {
  const std::string path = unique_path("default_ts");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, sizeof(std::uint32_t), 256);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    const std::uint32_t value = 1;
    producer.send_fixed(value, xproc::ipc::message_meta{});

    bool got = false;
    ASSERT_TRUE(consumer.poll([&](const xproc::ipc::message_meta& meta, void*, std::uint32_t) {
      got = true;
      EXPECT_NE(meta.timestamp_ns, 0u);
    }));
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// Default meta: user_data, flags, schema_id stay zero
// ============================================================

TEST(Meta, DefaultMetaHasZeroUserData) {
  const std::string path = unique_path("default_zero");
  xproc::core::shm::unlink(path);
  auto opts = fixed_opts(path, sizeof(std::uint32_t), 256);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    const std::uint32_t value = 1;
    producer.send_fixed(value, xproc::ipc::message_meta{});

    bool got = false;
    ASSERT_TRUE(consumer.poll([&](const xproc::ipc::message_meta& meta, void*, std::uint32_t) {
      got = true;
      EXPECT_EQ(meta.user_data, 0u);
      EXPECT_EQ(meta.flags, 0u);
      EXPECT_EQ(meta.schema_id, 0u);
      EXPECT_NE(meta.timestamp_ns, 0u);
    }));
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// Concurrent stress: single writer + single reader (fixed)
// ============================================================

TEST(Meta, ConcurrentSingleWriterMultipleReaders) {
  const std::string path = unique_path("concurrent_single");
  xproc::core::shm::unlink(path);
  constexpr std::size_t kDataCapacity = 65536;
  constexpr int kCount = 1000;

  auto opts = fixed_opts(path, sizeof(std::uint32_t), kDataCapacity);

  {
    xproc::ipc::producer producer(opts);
    xproc::ipc::consumer consumer(opts);

    std::atomic<int> errors{0};
    std::atomic<int> received{0};

    std::thread writer([&]() {
      for (int i = 0; i < kCount; ++i) {
        xproc::ipc::message_meta meta;
        meta.user_data = static_cast<std::uint64_t>(i);
        meta.flags = xproc::ipc::flag_priority_high;
        const std::uint32_t payload = static_cast<std::uint32_t>(i);
        producer.send_fixed(payload, meta);
      }
    });

    std::thread reader([&]() {
      int local_count = 0;
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
      while (local_count < kCount) {
        bool got = consumer.poll([&](const xproc::ipc::message_meta& meta, void* p, std::uint32_t len) {
          if (len != sizeof(std::uint32_t)) {
            ++errors;
            return;
          }
          std::uint32_t payload = 0;
          std::memcpy(&payload, p, sizeof(payload));

          if (meta.user_data != static_cast<std::uint64_t>(payload)) {
            ++errors;
          }
          if (!(meta.flags & xproc::ipc::flag_priority_high)) {
            ++errors;
          }
          ++local_count;
        });
        if (!got) {
          if (std::chrono::steady_clock::now() > deadline) {
            break;
          }
          std::this_thread::yield();
        }
      }
      received.store(local_count);
    });

    writer.join();
    reader.join();

    EXPECT_EQ(received.load(), kCount);
    EXPECT_EQ(errors.load(), 0);
  }
  xproc::core::shm::unlink(path);
}

// ============================================================
// Concurrent stress: 3 writers + 2 readers (varlen)
// ============================================================

TEST(Meta, ConcurrentMultiWriterMultiReader) {
  const std::string path = unique_path("concurrent_multi");
  xproc::core::shm::unlink(path);
  constexpr std::size_t kDataCapacity = 65536;
  constexpr int kWriters = 3;
  constexpr int kMsgsPerWriter = 200;
  constexpr int kTotalMsgs = kWriters * kMsgsPerWriter;
  constexpr int kReaders = 2;

  auto opts = varlen_opts(path, kDataCapacity);

  // Create the consumer first to establish the SHM before producers race on it.
  xproc::ipc::consumer consumer(opts);

  std::atomic<int> errors{0};
  std::atomic<int> total_received{0};
  std::mutex consumer_mtx;

  // Writers — each uses its own producer instance to exercise concurrent CAS on write_pos.
  std::vector<std::thread> writers;
  writers.reserve(kWriters);
  for (int w = 0; w < kWriters; ++w) {
    writers.emplace_back([&opts, w, kMsgsPerWriter]() {
      xproc::ipc::producer prod(opts);
      for (int i = 0; i < kMsgsPerWriter; ++i) {
        char buf[32];
        const int n = std::snprintf(buf, sizeof(buf), "w%d_msg_%d", w, i);
        xproc::ipc::message_meta meta;
        meta.user_data = static_cast<std::uint64_t>(w * kMsgsPerWriter + i);
        meta.schema_id = static_cast<std::uint32_t>(w);
        prod.send_varlen(buf, static_cast<std::uint32_t>(n) + 1, meta);
      }
    });
  }

  // Readers — share a single consumer (via mutex) to serialise SPSC read_pos
  // advancement.
  std::vector<std::thread> readers;
  readers.reserve(kReaders);
  for (int r = 0; r < kReaders; ++r) {
    readers.emplace_back([&]() {
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
      while (total_received.load(std::memory_order_acquire) < kTotalMsgs) {
        bool got = false;
        {
          std::lock_guard<std::mutex> lock(consumer_mtx);
          got = consumer.poll([&](const xproc::ipc::message_meta& meta, void*, std::uint32_t) {
            if (meta.schema_id >= static_cast<std::uint32_t>(kWriters)) {
              errors.fetch_add(1, std::memory_order_relaxed);
            }
            if (meta.timestamp_ns == 0) {
              errors.fetch_add(1, std::memory_order_relaxed);
            }
            total_received.fetch_add(1, std::memory_order_release);
          });
        }
        if (!got) {
          if (std::chrono::steady_clock::now() > deadline) {
            break;
          }
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto& w : writers) {
    w.join();
  }
  for (auto& r : readers) {
    r.join();
  }

  EXPECT_GE(total_received.load(), kTotalMsgs);
  EXPECT_EQ(errors.load(), 0);

  xproc::core::shm::unlink(path);
}
