# Producer Backpressure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add producer-side send control for full shared-memory rings: oversized fast-fail, non-blocking sends, bounded-time sends, and occupancy watermarks while preserving existing blocking send APIs.

**Architecture:** Keep `send_fixed*` and `send_varlen` as reliable blocking APIs. Add a small ringbuffer reservation-result layer, then wire it through `fixed_writer`, `varlen_writer`, and `ipc::channel`/`producer`. Watermark helpers read `write_pos - read_pos` snapshots from the shared control block and are advisory only.

**Tech Stack:** C++17, header-only xproc ringbuffer/ipc code, Google Test, Google Benchmark, CMake/CTest

---

## File Structure

- Create: `include/xproc/ringbuffer/reserve_result.hpp` — shared reservation status and payload pointer/position result for writer internals
- Create: `include/xproc/ipc/send_result.hpp` — public producer send result enum and string helper
- Modify: `include/xproc/ringbuffer/ringbuffer_error.hpp` — add timeout and oversized statuses for existing low-level diagnostics
- Modify: `include/xproc/ringbuffer/fixed_writer.hpp` — add oversized fast-fail, `try_reserve`, `reserve_for`, and keep blocking `reserve`
- Modify: `include/xproc/ringbuffer/varlen_writer.hpp` — same as fixed writer, including wrap/dummy-slot handling
- Modify: `include/xproc/ringbuffer/ringbuffer_view.hpp` — add advisory `used_bytes`, `available_bytes`, and `fill_ratio` helpers
- Modify: `include/xproc/ipc/channel.hpp` — add producer send-control APIs, fix `send_fixed_sized` slot stride, expose watermarks
- Modify: `include/xproc/xproc.hpp` — include the new public headers
- Create: `tests/producer_backpressure_test.cpp` — focused tests for fixed/varlen try-send, timeout, oversized, stride, and watermarks
- Modify: `tests/CMakeLists.txt` — register the new test in the portable test list
- Modify: `benchmarks/ipc_benchmark.cpp` — add no-pressure and full-ring `try_send` benchmarks

Reference files:

- `include/xproc/ringbuffer/fixed_reader.hpp` — consumer read advance and producer wakeup
- `include/xproc/ringbuffer/varlen_reader.hpp` — dummy-slot wrap behavior
- `include/xproc/shm/shm_layout.hpp` — `write_pos`, `read_pos`, `commit_seq`, `read_wake_seq`
- `tests/ringbuffer_full_ring_test.cpp` — existing full-ring blocking behavior

---

### Task 1: Add Shared Result Types

**Files:**
- Create: `include/xproc/ringbuffer/reserve_result.hpp`
- Create: `include/xproc/ipc/send_result.hpp`
- Modify: `include/xproc/ringbuffer/ringbuffer_error.hpp`
- Modify: `include/xproc/xproc.hpp`

- [ ] **Step 1: Add ringbuffer reservation result header**

Create `include/xproc/ringbuffer/reserve_result.hpp`:

```cpp
#pragma once

#include <cstdint>

namespace xproc {
namespace ringbuffer {

enum class reserve_status {
  ok = 0,
  full,
  timeout,
  message_too_large
};

struct reserve_result {
  reserve_status status{reserve_status::full};
  void* payload{nullptr};
  std::uint64_t position{0};

  explicit operator bool() const noexcept { return status == reserve_status::ok; }
};

inline const char* reserve_status_cstr(reserve_status status) noexcept {
  switch (status) {
    case reserve_status::ok:
      return "ok";
    case reserve_status::full:
      return "full";
    case reserve_status::timeout:
      return "timeout";
    case reserve_status::message_too_large:
      return "message_too_large";
    default:
      return "unknown";
  }
}

}  // namespace ringbuffer
}  // namespace xproc
```

- [ ] **Step 2: Add public IPC send result header**

Create `include/xproc/ipc/send_result.hpp`:

```cpp
#pragma once

namespace xproc {
namespace ipc {

enum class send_result {
  ok = 0,
  full,
  timeout,
  message_too_large,
  invalid_argument
};

inline const char* send_result_cstr(send_result result) noexcept {
  switch (result) {
    case send_result::ok:
      return "ok";
    case send_result::full:
      return "full";
    case send_result::timeout:
      return "timeout";
    case send_result::message_too_large:
      return "message_too_large";
    case send_result::invalid_argument:
      return "invalid_argument";
    default:
      return "unknown";
  }
}

}  // namespace ipc
}  // namespace xproc
```

- [ ] **Step 3: Extend existing ringbuffer error strings**

In `include/xproc/ringbuffer/ringbuffer_error.hpp`, replace the enum and string switch with:

```cpp
enum class ringbuffer_error {
  ok = 0,
  empty,
  full,
  timeout,
  message_too_large,
  incomplete,  // e.g. slot reserved but not yet committed (status != published)
  invalid_argument
};

inline const char* ringbuffer_error_cstr(ringbuffer_error e) noexcept {
  switch (e) {
    case ringbuffer_error::ok:
      return "ok";
    case ringbuffer_error::empty:
      return "empty";
    case ringbuffer_error::full:
      return "full";
    case ringbuffer_error::timeout:
      return "timeout";
    case ringbuffer_error::message_too_large:
      return "message_too_large";
    case ringbuffer_error::incomplete:
      return "incomplete";
    case ringbuffer_error::invalid_argument:
      return "invalid_argument";
    default:
      return "unknown";
  }
}
```

- [ ] **Step 4: Export new headers through umbrella include**

In `include/xproc/xproc.hpp`, add:

```cpp
#include <xproc/ipc/send_result.hpp>
```

immediately after:

```cpp
#include <xproc/ipc/runtime.hpp>
```

Then add:

```cpp
#include <xproc/ringbuffer/reserve_result.hpp>
```

immediately after:

```cpp
#include <xproc/ringbuffer/ringbuffer_error.hpp>
```

- [ ] **Step 5: Build to verify headers compile**

Run:

```bash
cmake --build build --target xproc_api_surface_test
```

Expected: build succeeds. If `build` does not exist, configure first:

```bash
cmake -S . -B build -DXPROC_BUILD_TESTS=ON -DXPROC_BUILD_BENCHMARKS=ON
cmake --build build --target xproc_api_surface_test
```

- [ ] **Step 6: Commit**

```bash
git add include/xproc/ringbuffer/reserve_result.hpp include/xproc/ipc/send_result.hpp include/xproc/ringbuffer/ringbuffer_error.hpp include/xproc/xproc.hpp
git commit -m "feat: add send control result types"
```

---

### Task 2: Add Ring Occupancy Helpers

**Files:**
- Modify: `include/xproc/ringbuffer/ringbuffer_view.hpp`
- Modify: `include/xproc/ipc/channel.hpp`
- Test: `tests/producer_backpressure_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Register the new test file**

In `tests/CMakeLists.txt`, add `producer_backpressure_test.cpp` to `XPROC_GTEST_TEST_FILES_COMMON`:

```cmake
set(XPROC_GTEST_TEST_FILES_COMMON
    api_surface_test.cpp
    ringbuffer_spsc_test.cpp
    layout_validate_test.cpp
    ipc_observer_attach_test.cpp
    protocol_codec_test.cpp
    socket_transport_test.cpp
    producer_backpressure_test.cpp
)
```

- [ ] **Step 2: Write failing watermark tests**

Create `tests/producer_backpressure_test.cpp`:

```cpp
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

}  // namespace

TEST(ProducerBackpressure, WatermarksTrackFixedOccupancy) {
  const std::string path = unique_path("watermarks");
  xproc::shm::shm::unlink(path);
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

  xproc::shm::shm::unlink(path);
}
```

- [ ] **Step 3: Run test to verify it fails**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R ProducerBackpressure.WatermarksTrackFixedOccupancy --output-on-failure
```

Expected: build fails with missing `capacity_bytes`, `used_bytes`, `available_bytes`, and `fill_ratio` members.

- [ ] **Step 4: Add advisory occupancy helpers to ringbuffer view**

In `include/xproc/ringbuffer/ringbuffer_view.hpp`, add this include:

```cpp
#include <atomic>
```

Then add these public methods directly below `capacity()`:

```cpp
  inline std::size_t used_bytes() const {
    const auto write = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    const auto used = write >= read ? (write - read) : 0;
    const auto cap = static_cast<std::uint64_t>(header_->data_capacity);
    return static_cast<std::size_t>(used > cap ? cap : used);
  }

  inline std::size_t available_bytes() const {
    return capacity() - used_bytes();
  }

  inline double fill_ratio() const {
    const auto cap = capacity();
    if (cap == 0) {
      return 0.0;
    }
    return static_cast<double>(used_bytes()) / static_cast<double>(cap);
  }
```

- [ ] **Step 5: Expose watermarks through channel and producer/consumer**

In `include/xproc/ipc/channel.hpp`, add these public methods before `send_fixed`:

```cpp
  std::size_t capacity_bytes() const {
    return header_ ? static_cast<std::size_t>(header_->data_capacity) : 0u;
  }

  std::size_t used_bytes() const {
    if (!header_) {
      return 0u;
    }
    const auto write = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    const auto used = write >= read ? (write - read) : 0;
    const auto cap = static_cast<std::uint64_t>(header_->data_capacity);
    return static_cast<std::size_t>(used > cap ? cap : used);
  }

  std::size_t available_bytes() const {
    return capacity_bytes() - used_bytes();
  }

  double fill_ratio() const {
    const auto cap = capacity_bytes();
    if (cap == 0) {
      return 0.0;
    }
    return static_cast<double>(used_bytes()) / static_cast<double>(cap);
  }
```

Add these `using` declarations to both `producer` and `consumer`:

```cpp
  using channel::available_bytes;
  using channel::capacity_bytes;
  using channel::fill_ratio;
  using channel::used_bytes;
```

- [ ] **Step 6: Run test to verify it passes**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R ProducerBackpressure.WatermarksTrackFixedOccupancy --output-on-failure
```

Expected: test passes.

- [ ] **Step 7: Commit**

```bash
git add include/xproc/ringbuffer/ringbuffer_view.hpp include/xproc/ipc/channel.hpp tests/CMakeLists.txt tests/producer_backpressure_test.cpp
git commit -m "feat: expose ring occupancy watermarks"
```

---

### Task 3: Implement Fixed Writer Try/Timeout Reserve And Slot Stride Fix

**Files:**
- Modify: `include/xproc/ringbuffer/fixed_writer.hpp`
- Modify: `include/xproc/ipc/channel.hpp`
- Test: `tests/producer_backpressure_test.cpp`

- [ ] **Step 1: Add failing fixed-channel tests**

Append to `tests/producer_backpressure_test.cpp`:

```cpp
TEST(ProducerBackpressure, TrySendFixedReportsFullWithoutBlocking) {
  const std::string path = unique_path("fixed_full");
  xproc::shm::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  const std::uint64_t a = 1;
  const std::uint64_t b = 2;
  const std::uint64_t c = 3;

  EXPECT_EQ(producer.try_send_fixed_sized(&a, sizeof(a)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_fixed_sized(&b, sizeof(b)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_fixed_sized(&c, sizeof(c)), xproc::ipc::send_result::full);

  xproc::shm::shm::unlink(path);
}

TEST(ProducerBackpressure, FixedSendForTimesOutWhenRingStaysFull) {
  const std::string path = unique_path("fixed_timeout");
  xproc::shm::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  const std::uint64_t a = 1;
  const std::uint64_t b = 2;
  const std::uint64_t c = 3;

  ASSERT_EQ(producer.try_send_fixed_sized(&a, sizeof(a)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_fixed_sized(&b, sizeof(b)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.send_fixed_sized_for(&c, sizeof(c), std::chrono::milliseconds(2)),
            xproc::ipc::send_result::timeout);

  xproc::shm::shm::unlink(path);
}

TEST(ProducerBackpressure, FixedOversizedMessageFailsImmediately) {
  const std::string path = unique_path("fixed_oversized");
  xproc::shm::shm::unlink(path);
  auto opts = fixed_opts(path, 64, 32);

  xproc::ipc::producer producer(opts);
  std::uint64_t value = 0;

  EXPECT_EQ(producer.try_send_fixed_sized(&value, sizeof(value)), xproc::ipc::send_result::message_too_large);

  xproc::shm::shm::unlink(path);
}

TEST(ProducerBackpressure, SendFixedSizedUsesConfiguredSlotStride) {
  const std::string path = unique_path("fixed_stride");
  xproc::shm::shm::unlink(path);
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

  xproc::shm::shm::unlink(path);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R "ProducerBackpressure.(TrySendFixedReportsFullWithoutBlocking|FixedSendForTimesOutWhenRingStaysFull|FixedOversizedMessageFailsImmediately|SendFixedSizedUsesConfiguredSlotStride)" --output-on-failure
```

Expected: build fails because `try_send_fixed_sized` and `send_fixed_sized_for` do not exist.

- [ ] **Step 3: Add fixed writer reserve variants**

In `include/xproc/ringbuffer/fixed_writer.hpp`, add includes:

```cpp
#include <chrono>
#include <stdexcept>
#include <thread>
#include <xproc/ringbuffer/reserve_result.hpp>
```

Replace the existing `reserve` method with:

```cpp
  void* reserve(uint32_t item_size, uint64_t& out_pos) {
    const uint32_t total_len = aligned_total_len(item_size);
    if (total_len > header_->data_capacity) {
      throw std::length_error("fixed_writer::reserve: message is larger than ring capacity");
    }
    while (true) {
      reserve_result rr = try_reserve(item_size);
      if (rr) {
        out_pos = rr.position;
        return rr.payload;
      }
      const uint32_t wake = header_->rb_meta.read_wake_seq.load(std::memory_order_relaxed);
      backoff_.pause(header_->rb_meta.read_wake_seq, wake);
    }
  }

  reserve_result try_reserve(uint32_t item_size) {
    const uint32_t total_len = aligned_total_len(item_size);
    if (total_len > header_->data_capacity) {
      return {reserve_status::message_too_large, nullptr, 0};
    }

    uint64_t curr_write = header_->rb_meta.write_pos.load(std::memory_order_relaxed);
    uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    if (curr_write + total_len - curr_read > header_->data_capacity) {
      return {reserve_status::full, nullptr, 0};
    }

    if (!header_->rb_meta.write_pos.compare_exchange_strong(curr_write, curr_write + total_len)) {
      return {reserve_status::full, nullptr, 0};
    }

    auto* h = reinterpret_cast<details::fixed_message_header*>(get_ptr(curr_write));
    h->status.store(0, std::memory_order_relaxed);
    return {reserve_status::ok, get_ptr(curr_write + sizeof(details::fixed_message_header)), curr_write};
  }

  template <typename Rep, typename Period>
  reserve_result reserve_for(uint32_t item_size, const std::chrono::duration<Rep, Period>& timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      reserve_result rr = try_reserve(item_size);
      if (rr.status != reserve_status::full) {
        return rr;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return {reserve_status::timeout, nullptr, 0};
      }
      std::this_thread::yield();
    }
  }
```

Add this private helper before `backoff_`:

```cpp
  uint32_t aligned_total_len(uint32_t item_size) const noexcept {
    return align_size(item_size + sizeof(details::fixed_message_header));
  }
```

- [ ] **Step 4: Add fixed send-control APIs and fix stride**

In `include/xproc/ipc/channel.hpp`, add includes:

```cpp
#include <chrono>
#include <xproc/ipc/send_result.hpp>
```

Add this private helper to `channel`:

```cpp
  static send_result map_reserve_status(ringbuffer::reserve_status status) noexcept {
    switch (status) {
      case ringbuffer::reserve_status::ok:
        return send_result::ok;
      case ringbuffer::reserve_status::full:
        return send_result::full;
      case ringbuffer::reserve_status::timeout:
        return send_result::timeout;
      case ringbuffer::reserve_status::message_too_large:
        return send_result::message_too_large;
      default:
        return send_result::invalid_argument;
    }
  }
```

Add this public helper near the fixed send methods:

```cpp
  send_result try_send_fixed_sized(const void* data, std::uint32_t byte_length) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::try_send_fixed_sized requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::try_send_fixed_sized requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      return send_result::invalid_argument;
    }
    auto* fw = static_cast<ringbuffer::fixed_writer*>(writer_.get());
    auto rr = fw->try_reserve(opts_.item_size);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(rr.payload) + byte_length, 0,
                  static_cast<std::size_t>(opts_.item_size - byte_length));
    }
    fw->commit(rr.position);
    return send_result::ok;
  }

  template <typename T>
  bool try_send_fixed(const T& data) {
    return try_send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T))) == send_result::ok;
  }

  template <typename Rep, typename Period>
  send_result send_fixed_sized_for(const void* data,
                                   std::uint32_t byte_length,
                                   const std::chrono::duration<Rep, Period>& timeout) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_sized_for requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_sized_for requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      return send_result::invalid_argument;
    }
    auto* fw = static_cast<ringbuffer::fixed_writer*>(writer_.get());
    auto rr = fw->reserve_for(opts_.item_size, timeout);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(rr.payload) + byte_length, 0,
                  static_cast<std::size_t>(opts_.item_size - byte_length));
    }
    fw->commit(rr.position);
    return send_result::ok;
  }

  template <typename T, typename Rep, typename Period>
  send_result send_fixed_for(const T& data, const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(&data, static_cast<std::uint32_t>(sizeof(T)), timeout);
  }
```

Update blocking `send_fixed_sized` to reserve the configured fixed slot size and zero-pad:

```cpp
    void* buf = fw->reserve(opts_.item_size, pos);
    std::memcpy(buf, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(buf) + byte_length, 0,
                  static_cast<std::size_t>(opts_.item_size - byte_length));
    }
```

Add producer `using` declarations:

```cpp
  using channel::send_fixed_for;
  using channel::send_fixed_sized_for;
  using channel::try_send_fixed;
  using channel::try_send_fixed_sized;
```

- [ ] **Step 5: Run fixed tests**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R "ProducerBackpressure.(TrySendFixedReportsFullWithoutBlocking|FixedSendForTimesOutWhenRingStaysFull|FixedOversizedMessageFailsImmediately|SendFixedSizedUsesConfiguredSlotStride)" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/xproc/ringbuffer/fixed_writer.hpp include/xproc/ipc/channel.hpp tests/producer_backpressure_test.cpp
git commit -m "feat: add fixed producer send control"
```

---

### Task 4: Implement Fixed Bytes API Variants

**Files:**
- Modify: `include/xproc/ipc/channel.hpp`
- Test: `tests/producer_backpressure_test.cpp`

- [ ] **Step 1: Add failing tests for fixed bytes variants**

Append to `tests/producer_backpressure_test.cpp`:

```cpp
TEST(ProducerBackpressure, TrySendFixedBytesPadsPayload) {
  const std::string path = unique_path("fixed_bytes");
  xproc::shm::shm::unlink(path);
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

  xproc::shm::shm::unlink(path);
}

TEST(ProducerBackpressure, SendFixedBytesForCanTimeout) {
  const std::string path = unique_path("fixed_bytes_timeout");
  xproc::shm::shm::unlink(path);
  auto opts = fixed_opts(path, 8, 32);

  xproc::ipc::producer producer(opts);
  const std::uint64_t value = 7;
  ASSERT_EQ(producer.try_send_fixed_bytes(&value, sizeof(value)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_fixed_bytes(&value, sizeof(value)), xproc::ipc::send_result::ok);

  EXPECT_EQ(producer.send_fixed_bytes_for(&value, sizeof(value), std::chrono::milliseconds(2)),
            xproc::ipc::send_result::timeout);

  xproc::shm::shm::unlink(path);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R "ProducerBackpressure.(TrySendFixedBytesPadsPayload|SendFixedBytesForCanTimeout)" --output-on-failure
```

Expected: build fails because `try_send_fixed_bytes` and `send_fixed_bytes_for` do not exist.

- [ ] **Step 3: Implement fixed bytes variants**

In `include/xproc/ipc/channel.hpp`, add:

```cpp
  send_result try_send_fixed_bytes(const void* data, std::uint32_t payload_len) {
    return try_send_fixed_sized(data, payload_len);
  }

  template <typename Rep, typename Period>
  send_result send_fixed_bytes_for(const void* data,
                                   std::uint32_t payload_len,
                                   const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(data, payload_len, timeout);
  }
```

Add producer `using` declarations:

```cpp
  using channel::send_fixed_bytes_for;
  using channel::try_send_fixed_bytes;
```

- [ ] **Step 4: Run fixed bytes tests**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R "ProducerBackpressure.(TrySendFixedBytesPadsPayload|SendFixedBytesForCanTimeout)" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/xproc/ipc/channel.hpp tests/producer_backpressure_test.cpp
git commit -m "feat: add fixed bytes send control"
```

---

### Task 5: Implement Varlen Try/Timeout Send

**Files:**
- Modify: `include/xproc/ringbuffer/varlen_writer.hpp`
- Modify: `include/xproc/ipc/channel.hpp`
- Test: `tests/producer_backpressure_test.cpp`

- [ ] **Step 1: Add failing varlen tests**

Append to `tests/producer_backpressure_test.cpp`:

```cpp
namespace {

xproc::ipc::transport_options varlen_opts(const std::string& path, std::size_t capacity) {
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(capacity);
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = true;
  return opts;
}

}  // namespace

TEST(ProducerBackpressure, TrySendVarlenReportsFullWithoutBlocking) {
  const std::string path = unique_path("varlen_full");
  xproc::shm::shm::unlink(path);
  auto opts = varlen_opts(path, 32);

  xproc::ipc::producer producer(opts);
  const char payload[8] = {'x', 'p', 'r', 'o', 'c', '1', '2', '3'};

  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);
  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::full);

  xproc::shm::shm::unlink(path);
}

TEST(ProducerBackpressure, VarlenSendForTimesOutWhenRingStaysFull) {
  const std::string path = unique_path("varlen_timeout");
  xproc::shm::shm::unlink(path);
  auto opts = varlen_opts(path, 32);

  xproc::ipc::producer producer(opts);
  const char payload[8] = {'x', 'p', 'r', 'o', 'c', '1', '2', '3'};
  ASSERT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);
  ASSERT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::ok);

  EXPECT_EQ(producer.send_varlen_for(payload, sizeof(payload), std::chrono::milliseconds(2)),
            xproc::ipc::send_result::timeout);

  xproc::shm::shm::unlink(path);
}

TEST(ProducerBackpressure, VarlenOversizedMessageFailsImmediately) {
  const std::string path = unique_path("varlen_oversized");
  xproc::shm::shm::unlink(path);
  auto opts = varlen_opts(path, 32);

  xproc::ipc::producer producer(opts);
  char payload[64]{};

  EXPECT_EQ(producer.try_send_varlen(payload, sizeof(payload)), xproc::ipc::send_result::message_too_large);

  xproc::shm::shm::unlink(path);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R "ProducerBackpressure.(TrySendVarlenReportsFullWithoutBlocking|VarlenSendForTimesOutWhenRingStaysFull|VarlenOversizedMessageFailsImmediately)" --output-on-failure
```

Expected: build fails because varlen send-control APIs do not exist.

- [ ] **Step 3: Add varlen writer reserve variants**

In `include/xproc/ringbuffer/varlen_writer.hpp`, add includes:

```cpp
#include <chrono>
#include <stdexcept>
#include <thread>
#include <xproc/ringbuffer/reserve_result.hpp>
```

Replace `reserve` with:

```cpp
  void* reserve(uint32_t len, uint64_t& out_pos) {
    const uint32_t total_len = aligned_total_len(len);
    if (total_len > header_->data_capacity) {
      throw std::length_error("varlen_writer::reserve: message is larger than ring capacity");
    }
    while (true) {
      reserve_result rr = try_reserve(len);
      if (rr) {
        out_pos = rr.position;
        return rr.payload;
      }
      const uint32_t wake = header_->rb_meta.read_wake_seq.load(std::memory_order_relaxed);
      backoff_.pause(header_->rb_meta.read_wake_seq, wake);
    }
  }

  reserve_result try_reserve(uint32_t len) {
    const uint32_t total_len = aligned_total_len(len);
    if (total_len > header_->data_capacity) {
      return {reserve_status::message_too_large, nullptr, 0};
    }

    uint64_t curr_write = header_->rb_meta.write_pos.load(std::memory_order_relaxed);
    uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_acquire);

    if (curr_write + total_len - curr_read > header_->data_capacity) {
      return {reserve_status::full, nullptr, 0};
    }

    uint64_t to_end = bytes_to_end(curr_write);
    if (to_end < total_len) {
      if (header_->rb_meta.write_pos.compare_exchange_strong(curr_write, curr_write + to_end)) {
        auto* h = reinterpret_cast<details::varlen_message_header*>(get_ptr(curr_write));
        h->status.store(2, std::memory_order_release);
      }
      return {reserve_status::full, nullptr, 0};
    }

    if (!header_->rb_meta.write_pos.compare_exchange_strong(curr_write, curr_write + total_len)) {
      return {reserve_status::full, nullptr, 0};
    }

    auto* h = reinterpret_cast<details::varlen_message_header*>(get_ptr(curr_write));
    h->length = len;
    h->status.store(0, std::memory_order_relaxed);
    return {reserve_status::ok, get_ptr(curr_write + sizeof(details::varlen_message_header)), curr_write};
  }

  template <typename Rep, typename Period>
  reserve_result reserve_for(uint32_t len, const std::chrono::duration<Rep, Period>& timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      reserve_result rr = try_reserve(len);
      if (rr.status != reserve_status::full) {
        return rr;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return {reserve_status::timeout, nullptr, 0};
      }
      std::this_thread::yield();
    }
  }
```

Add this private helper:

```cpp
  uint32_t aligned_total_len(uint32_t len) const noexcept {
    return align_size(len + sizeof(details::varlen_message_header));
  }
```

- [ ] **Step 4: Add varlen send-control APIs**

In `include/xproc/ipc/channel.hpp`, add:

```cpp
  send_result try_send_varlen(const void* data, std::uint32_t len) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::try_send_varlen requires producer role");
    }
    if (opts_.type != channel_type::varlen) {
      throw std::logic_error("channel::try_send_varlen requires variable channel");
    }
    auto* vw = static_cast<ringbuffer::varlen_writer*>(writer_.get());
    auto rr = vw->try_reserve(len);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, len);
    vw->commit(rr.position);
    return send_result::ok;
  }

  template <typename Rep, typename Period>
  send_result send_varlen_for(const void* data,
                              std::uint32_t len,
                              const std::chrono::duration<Rep, Period>& timeout) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_varlen_for requires producer role");
    }
    if (opts_.type != channel_type::varlen) {
      throw std::logic_error("channel::send_varlen_for requires variable channel");
    }
    auto* vw = static_cast<ringbuffer::varlen_writer*>(writer_.get());
    auto rr = vw->reserve_for(len, timeout);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, len);
    vw->commit(rr.position);
    return send_result::ok;
  }
```

Add producer `using` declarations:

```cpp
  using channel::send_varlen_for;
  using channel::try_send_varlen;
```

- [ ] **Step 5: Run varlen tests**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R "ProducerBackpressure.(TrySendVarlenReportsFullWithoutBlocking|VarlenSendForTimesOutWhenRingStaysFull|VarlenOversizedMessageFailsImmediately)" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/xproc/ringbuffer/varlen_writer.hpp include/xproc/ipc/channel.hpp tests/producer_backpressure_test.cpp
git commit -m "feat: add varlen producer send control"
```

---

### Task 6: Add Timeout Success Tests And Benchmarks

**Files:**
- Modify: `tests/producer_backpressure_test.cpp`
- Modify: `benchmarks/ipc_benchmark.cpp`
- Modify: `benchmarks/CMakeLists.txt` only if a separate benchmark file is created; this plan extends the existing file

- [ ] **Step 1: Add timeout success test**

Append to `tests/producer_backpressure_test.cpp`:

```cpp
TEST(ProducerBackpressure, FixedSendForSucceedsWhenConsumerDrains) {
  const std::string path = unique_path("fixed_timeout_success");
  xproc::shm::shm::unlink(path);
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

  EXPECT_EQ(producer.send_fixed_sized_for(&c, sizeof(c), std::chrono::milliseconds(100)),
            xproc::ipc::send_result::ok);
  drain.join();

  xproc::shm::shm::unlink(path);
}
```

- [ ] **Step 2: Run all producer backpressure tests**

Run:

```bash
cmake --build build --target xproc_producer_backpressure_test
ctest --test-dir build -R ProducerBackpressure --output-on-failure
```

Expected: all `ProducerBackpressure.*` tests pass.

- [ ] **Step 3: Add IPC send-control benchmarks**

In `benchmarks/ipc_benchmark.cpp`, add after `BM_FixedSendPoll`:

```cpp
static void BM_FixedTrySendPoll(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  if (payload_len == 0 || payload_len > 1024) {
    state.SkipWithError("payload_len out of expected bounds");
    return;
  }

  const std::string path = unique_path("fixed_try", static_cast<int>(payload_len));
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  std::vector<std::byte> payload(payload_len, std::byte{0x5a});

  for (auto _ : state) {
    const auto sent = producer.try_send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    if (sent != xproc::ipc::send_result::ok) {
      state.SkipWithError("try_send unexpectedly failed");
      break;
    }
    bool got = false;
    while (!got) {
      got = consumer.poll([&](void*, std::uint32_t len) { benchmark::DoNotOptimize(len); });
      if (!got) {
        const auto c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::shm::shm::unlink(path);
}

static void BM_FixedTrySendFull(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  if (payload_len == 0 || payload_len > 1024) {
    state.SkipWithError("payload_len out of expected bounds");
    return;
  }

  const std::string path = unique_path("fixed_try_full", static_cast<int>(payload_len));
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 64;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  std::vector<std::byte> payload(payload_len, std::byte{0x5a});
  while (producer.try_send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len)) ==
         xproc::ipc::send_result::ok) {
  }

  for (auto _ : state) {
    const auto sent = producer.try_send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    benchmark::DoNotOptimize(sent);
    if (sent != xproc::ipc::send_result::full &&
        sent != xproc::ipc::send_result::message_too_large) {
      state.SkipWithError("expected full or oversized result");
      break;
    }
  }

  state.SetItemsProcessed(state.iterations());
  xproc::shm::shm::unlink(path);
}
```

Add these benchmark registrations at the bottom with the existing `BENCHMARK(...)` registrations:

```cpp
BENCHMARK(BM_FixedTrySendPoll)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_FixedTrySendFull)->Arg(16)->Arg(32)->Unit(benchmark::kNanosecond);
```

- [ ] **Step 4: Run benchmark build and smoke run**

Run:

```bash
cmake --build build --target xproc_bench_ipc
./build/benchmarks/xproc_bench_ipc --benchmark_filter='Fixed(TrySendPoll|TrySendFull)' --benchmark_min_time=0.01s
```

Expected: benchmark binary builds and prints results for `BM_FixedTrySendPoll` and `BM_FixedTrySendFull`.

- [ ] **Step 5: Run full relevant regression set**

Run:

```bash
cmake --build build --target xproc_run_tests
cmake --build build --target xproc_bench_ipc
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add tests/producer_backpressure_test.cpp benchmarks/ipc_benchmark.cpp
git commit -m "test: cover producer backpressure behavior"
```

---

## Final Verification

- [ ] **Step 1: Run format if the project has a configured formatter target**

Run:

```bash
cmake --build build --target xproc_run_tests
```

Expected: target builds tests and CTest reports success.

- [ ] **Step 2: Inspect public API exposure**

Run:

```bash
rg -n "send_result|try_send_|send_.*_for|capacity_bytes|used_bytes|available_bytes|fill_ratio" include tests benchmarks
```

Expected: result types are in `include/xproc/ipc/send_result.hpp`, reserve internals are in ringbuffer headers, send APIs are exposed through `producer`, and tests cover the new behavior.

- [ ] **Step 3: Confirm no unexpected worktree changes**

Run:

```bash
git status --short
```

Expected: no output.
