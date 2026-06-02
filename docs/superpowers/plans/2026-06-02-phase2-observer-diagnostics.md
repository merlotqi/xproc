# Observer Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add derived diagnostic helpers to the observer class and a stateful tracker for producer liveness, exposed through C++ and C API.

**Architecture:** Stateless diagnostics (occupancy, lag) go directly on the `observer` class as methods. A new `diagnostics_tracker` class handles stateful comparisons (liveness, idle duration). C API exposes both via observer-handle functions and an opaque tracker handle.

**Tech Stack:** C++20, Google Test, C API wrapper

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `include/xproc/ipc/observer.hpp` | Add 4 stateless diagnostic methods |
| Create | `include/xproc/ipc/diagnostics_tracker.hpp` | New `diagnostics_tracker` class declaration |
| Create | `src/ipc/diagnostics_tracker.cpp` | `diagnostics_tracker` implementation |
| Modify | `capi/xproc_c.h` | Add 9 new C API function declarations + opaque type |
| Modify | `capi/xproc_c.cpp` | Implement 9 new C API functions |
| Create | `tests/ipc_diagnostics_test.cpp` | C++ unit tests for observer diagnostics + tracker |
| Modify | `tests/capi_smoke_test.cpp` | Add C API smoke tests for diagnostics |
| Modify | `tests/CMakeLists.txt` | Register new test executable |

---

### Task 1: Stateless diagnostics on observer

**Files:**
- Modify: `include/xproc/ipc/observer.hpp:97-105` (add methods before `private:`)
- Create: `tests/ipc_diagnostics_test.cpp`
- Modify: `tests/CMakeLists.txt:23-32` (add test file to common list)

- [ ] **Step 1: Write failing tests for observer diagnostics**

Create `tests/ipc_diagnostics_test.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <thread>
#include <chrono>
#include <xproc/xproc.hpp>

static xproc::ipc::transport_options make_test_opts(const std::string& path, std::size_t data_capacity) {
  xproc::core::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + data_capacity;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  return opts;
}

TEST(ObserverDiagnostics, OccupancyRatioEmptyRing) {
  const std::string path = "/xproc_diag_occ_ratio_empty";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::observer obs(opts);
    EXPECT_DOUBLE_EQ(obs.occupancy_ratio(), 0.0);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyRatioPartialFill) {
  const std::string path = "/xproc_diag_occ_ratio_partial";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);
    double ratio = obs.occupancy_ratio();
    EXPECT_GT(ratio, 0.0);
    EXPECT_LT(ratio, 1.0);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyRatioFullRing) {
  const std::string path = "/xproc_diag_occ_ratio_full";
  // Small ring: control_block + enough for a few fixed slots
  auto opts = make_test_opts(path, 64);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    // Fill until try_send fails
    while (prod.try_send_fixed<std::uint32_t>(1u) == xproc::ipc::send_result::ok) {
    }
    double ratio = obs.occupancy_ratio();
    EXPECT_GE(ratio, 0.9);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyBytesEmptyRing) {
  const std::string path = "/xproc_diag_occ_bytes_empty";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::observer obs(opts);
    EXPECT_EQ(obs.occupancy_bytes(), 0u);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyBytesAfterSend) {
  const std::string path = "/xproc_diag_occ_bytes_send";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);
    EXPECT_GT(obs.occupancy_bytes(), 0u);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, AvailableBytesEmptyRing) {
  const std::string path = "/xproc_diag_avail_empty";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::observer obs(opts);
    // available_bytes should equal data_capacity when empty
    EXPECT_EQ(obs.available_bytes(), static_cast<std::uint64_t>(opts.shm_size - sizeof(xproc::core::control_block)));
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, ConsumerLagBytesNoConsumer) {
  const std::string path = "/xproc_diag_lag_no_cons";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);
    // With no consumer, lag == occupancy_bytes
    EXPECT_EQ(obs.consumer_lag_bytes(), obs.occupancy_bytes());
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, ConsumerLagBytesAfterConsume) {
  const std::string path = "/xproc_diag_lag_after_consume";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);
    xproc::ipc::consumer cons(opts);

    prod.send_fixed<std::uint32_t>(42u);
    const auto lag_before = obs.consumer_lag_bytes();
    EXPECT_GT(lag_before, 0u);

    // Consume the message
    cons.poll([](void*, std::uint32_t) {});
    const auto lag_after = obs.consumer_lag_bytes();
    EXPECT_LT(lag_after, lag_before);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, ObserverDiagnosticsMatchManualCalc) {
  const std::string path = "/xproc_diag_match_manual";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);

    const auto snap = obs.snapshot();
    const auto cap = static_cast<std::uint64_t>(obs.header()->data_capacity);
    const auto used = snap.write_pos - snap.read_pos;
    const auto expected = (used > cap) ? cap : used;

    EXPECT_EQ(obs.occupancy_bytes(), expected);
    EXPECT_EQ(obs.consumer_lag_bytes(), expected);
    EXPECT_DOUBLE_EQ(obs.occupancy_ratio(), static_cast<double>(expected) / static_cast<double>(cap));
  }
  xproc::core::shm::unlink(path);
}
```

- [ ] **Step 2: Add test file to CMakeLists.txt**

Add `ipc_diagnostics_test.cpp` to the `XPROC_GTEST_TEST_FILES_COMMON` list in `tests/CMakeLists.txt` after `ipc_observer_attach_test.cpp`:

```cmake
set(XPROC_GTEST_TEST_FILES_COMMON
    api_surface_test.cpp
    ringbuffer_spsc_test.cpp
    layout_validate_test.cpp
    ipc_observer_attach_test.cpp
    ipc_diagnostics_test.cpp
    protocol_codec_test.cpp
    socket_transport_test.cpp
    runtime_allocation_test.cpp
    producer_backpressure_test.cpp
)
```

- [ ] **Step 3: Build and verify tests fail**

Run: `cmake --build build --target xproc_ipc_diagnostics_tests 2>&1 | tail -20`
Expected: Compile error — `occupancy_ratio`, `occupancy_bytes`, `available_bytes`, `consumer_lag_bytes` are not members of `xproc::ipc::observer`.

- [ ] **Step 4: Implement stateless diagnostics on observer**

Add four methods to `observer` in `include/xproc/ipc/observer.hpp`, before the `private:` section (after line 97):

```cpp
  double occupancy_ratio() const {
    if (!header_) return 0.0;
    const auto cap = header_->data_capacity;
    if (cap == 0) return 0.0;
    const auto wp = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto rp = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    auto used = wp - rp;
    if (used > cap) used = cap;
    return static_cast<double>(used) / static_cast<double>(cap);
  }

  std::uint64_t occupancy_bytes() const {
    if (!header_) return 0;
    const auto cap = header_->data_capacity;
    const auto wp = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto rp = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    auto used = wp - rp;
    if (used > cap) used = cap;
    return used;
  }

  std::uint64_t available_bytes() const {
    if (!header_) return 0;
    const auto cap = header_->data_capacity;
    const auto wp = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto rp = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    auto used = wp - rp;
    if (used > cap) used = cap;
    return cap - used;
  }

  std::uint64_t consumer_lag_bytes() const {
    if (!header_) return 0;
    const auto cap = header_->data_capacity;
    const auto wp = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto rp = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    auto lag = wp - rp;
    if (lag > cap) lag = cap;
    return lag;
  }
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build --target xproc_ipc_diagnostics_tests && cd build && ctest -R ipc_diagnostics --output-on-failure`
Expected: All 9 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/xproc/ipc/observer.hpp tests/ipc_diagnostics_test.cpp tests/CMakeLists.txt
git commit -m "feat: add stateless diagnostic methods to observer

Add occupancy_ratio(), occupancy_bytes(), available_bytes(), and
consumer_lag_bytes() to the observer class, matching the existing
watermark helpers on channel/producer/consumer.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: diagnostics_tracker class

**Files:**
- Create: `include/xproc/ipc/diagnostics_tracker.hpp`
- Create: `src/ipc/diagnostics_tracker.cpp`
- Modify: `tests/ipc_diagnostics_test.cpp` (add tracker tests)
- Modify: `CMakeLists.txt` or `src/ipc/CMakeLists.txt` (add source file if needed)

- [ ] **Step 1: Write failing tests for diagnostics_tracker**

Append to `tests/ipc_diagnostics_test.cpp`:

```cpp
#include <xproc/ipc/diagnostics_tracker.hpp>

TEST(DiagnosticsTracker, ProducerAliveOnCommit) {
  const std::string path = "/xproc_diag_tracker_alive";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    // Send a message — commit_seq should advance
    prod.send_fixed<std::uint32_t>(42u);
    tracker.update(obs.snapshot());
    EXPECT_TRUE(tracker.producer_alive());
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, ProducerIdleNoActivity) {
  const std::string path = "/xproc_diag_tracker_idle";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    // No activity — update twice with same state
    tracker.update(obs.snapshot());
    EXPECT_FALSE(tracker.producer_alive());
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, IdleDurationIncreases) {
  const std::string path = "/xproc_diag_tracker_idle_dur";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(tracker.idle_duration_ms(), 40u);
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, IdleDurationResetsOnProgress) {
  const std::string path = "/xproc_diag_tracker_idle_reset";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send a message — write_pos advances, idle should reset
    prod.send_fixed<std::uint32_t>(42u);
    tracker.update(obs.snapshot());
    EXPECT_LT(tracker.idle_duration_ms(), 50u);
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, MultipleUpdates) {
  const std::string path = "/xproc_diag_tracker_multi";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap0 = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap0, obs.header()->data_capacity);

    prod.send_fixed<std::uint32_t>(1u);
    tracker.update(obs.snapshot());
    const auto prev1 = tracker.previous();
    EXPECT_EQ(prev1.write_pos, snap0.write_pos);

    prod.send_fixed<std::uint32_t>(2u);
    tracker.update(obs.snapshot());
    const auto prev2 = tracker.previous();
    // prev2 should be the snapshot from after the first send
    EXPECT_GT(prev2.write_pos, prev1.write_pos);
    EXPECT_GT(tracker.current().write_pos, prev2.write_pos);
  }
  xproc::core::shm::unlink(path);
}
```

- [ ] **Step 2: Build and verify tests fail**

Run: `cmake --build build --target xproc_ipc_diagnostics_tests 2>&1 | tail -20`
Expected: Compile error — `diagnostics_tracker.hpp` not found.

- [ ] **Step 3: Create diagnostics_tracker header**

Create `include/xproc/ipc/diagnostics_tracker.hpp`:

```cpp
#pragma once

#include <chrono>
#include <cstdint>
#include <xproc/ipc/inspector.hpp>

namespace xproc::ipc {

class diagnostics_tracker {
public:
  explicit diagnostics_tracker(const ring_snapshot& initial, std::uint64_t data_capacity)
      : prev_(initial), curr_(initial), data_capacity_(data_capacity),
        last_progress_(std::chrono::steady_clock::now()) {}

  void update(const ring_snapshot& snap);

  bool producer_alive() const noexcept { return curr_.commit_seq != prev_.commit_seq; }

  std::uint64_t idle_duration_ms() const;

  const ring_snapshot& current() const noexcept { return curr_; }
  const ring_snapshot& previous() const noexcept { return prev_; }
  std::uint64_t data_capacity() const noexcept { return data_capacity_; }

private:
  ring_snapshot prev_;
  ring_snapshot curr_;
  std::uint64_t data_capacity_;
  std::chrono::steady_clock::time_point last_progress_;
};

} // namespace xproc::ipc
```

- [ ] **Step 4: Create diagnostics_tracker implementation**

Create `src/ipc/diagnostics_tracker.cpp`:

```cpp
#include <xproc/ipc/diagnostics_tracker.hpp>

namespace xproc::ipc {

void diagnostics_tracker::update(const ring_snapshot& snap) {
  prev_ = curr_;
  curr_ = snap;
  if (curr_.write_pos != prev_.write_pos || curr_.read_pos != prev_.read_pos) {
    last_progress_ = std::chrono::steady_clock::now();
  }
}

std::uint64_t diagnostics_tracker::idle_duration_ms() const {
  const auto elapsed = std::chrono::steady_clock::now() - last_progress_;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

} // namespace xproc::ipc
```

- [ ] **Step 5: Add source file to build**

Check if `src/ipc/` sources are globbed or listed explicitly. If listed, add `diagnostics_tracker.cpp`. If globbed, it will be picked up automatically. Verify by checking the root or src `CMakeLists.txt`.

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build --target xproc_ipc_diagnostics_tests && cd build && ctest -R ipc_diagnostics --output-on-failure`
Expected: All 14 tests PASS (9 observer diagnostics + 5 tracker).

- [ ] **Step 7: Commit**

```bash
git add include/xproc/ipc/diagnostics_tracker.hpp src/ipc/diagnostics_tracker.cpp tests/ipc_diagnostics_test.cpp
git commit -m "feat: add diagnostics_tracker for producer liveness and idle tracking

New stateful class that compares successive ring_snapshot values to
detect producer liveness (commit_seq delta) and idle duration
(time since last write_pos/read_pos change).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: C API exposure

**Files:**
- Modify: `capi/xproc_c.h:449-456` (add declarations before `#ifdef __cplusplus`)
- Modify: `capi/xproc_c.cpp:749` (add implementations after `xproc_c_observer_peek_copy`)
- Modify: `tests/capi_smoke_test.cpp` (add smoke tests)

- [ ] **Step 1: Write failing C API smoke tests**

Append to `tests/capi_smoke_test.cpp`:

```cpp
TEST(CApiSmoke, COccupancyRatioSmoke) {
  const char* path = "/xproc_capi_occ_ratio";
  xproc_c_shm_unlink(path);

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512;
  opts.type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(uint32_t);

  xproc_c_producer* prod = nullptr;
  xproc_c_observer* obs = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&opts, &prod), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_observer_open(&opts, &obs), XPROC_C_STATUS_OK);

  uint32_t val = 42;
  ASSERT_EQ(xproc_c_producer_send_fixed(prod, &val, sizeof(val)), XPROC_C_STATUS_OK);

  double ratio = 0.0;
  ASSERT_EQ(xproc_c_observer_occupancy_ratio(obs, &ratio), XPROC_C_STATUS_OK);
  EXPECT_GT(ratio, 0.0);

  xproc_c_observer_close(obs);
  xproc_c_producer_close(prod);
  xproc_c_shm_unlink(path);
}

TEST(CApiSmoke, COccupancyBytesSmoke) {
  const char* path = "/xproc_capi_occ_bytes";
  xproc_c_shm_unlink(path);

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512;
  opts.type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(uint32_t);

  xproc_c_observer* obs = nullptr;
  ASSERT_EQ(xproc_c_observer_open(&opts, &obs), XPROC_C_STATUS_OK);

  uint64_t bytes = 999;
  ASSERT_EQ(xproc_c_observer_occupancy_bytes(obs, &bytes), XPROC_C_STATUS_OK);
  EXPECT_EQ(bytes, 0u);

  xproc_c_observer_close(obs);
  xproc_c_shm_unlink(path);
}

TEST(CApiSmoke, CAvailableBytesSmoke) {
  const char* path = "/xproc_capi_avail_bytes";
  xproc_c_shm_unlink(path);

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512;
  opts.type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(uint32_t);

  xproc_c_observer* obs = nullptr;
  ASSERT_EQ(xproc_c_observer_open(&opts, &obs), XPROC_C_STATUS_OK);

  uint64_t avail = 0;
  ASSERT_EQ(xproc_c_observer_available_bytes(obs, &avail), XPROC_C_STATUS_OK);
  EXPECT_GT(avail, 0u);

  xproc_c_observer_close(obs);
  xproc_c_shm_unlink(path);
}

TEST(CApiSmoke, CConsumerLagBytesSmoke) {
  const char* path = "/xproc_capi_lag_bytes";
  xproc_c_shm_unlink(path);

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512;
  opts.type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(uint32_t);

  xproc_c_producer* prod = nullptr;
  xproc_c_observer* obs = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&opts, &prod), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_observer_open(&opts, &obs), XPROC_C_STATUS_OK);

  uint32_t val = 42;
  ASSERT_EQ(xproc_c_producer_send_fixed(prod, &val, sizeof(val)), XPROC_C_STATUS_OK);

  uint64_t lag = 0;
  ASSERT_EQ(xproc_c_observer_consumer_lag_bytes(obs, &lag), XPROC_C_STATUS_OK);
  EXPECT_GT(lag, 0u);

  xproc_c_observer_close(obs);
  xproc_c_producer_close(prod);
  xproc_c_shm_unlink(path);
}

TEST(CApiSmoke, CTrackerCreateUpdateDestroy) {
  const char* path = "/xproc_capi_tracker_lifecycle";
  xproc_c_shm_unlink(path);

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512;
  opts.type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(uint32_t);

  xproc_c_observer* obs = nullptr;
  ASSERT_EQ(xproc_c_observer_open(&opts, &obs), XPROC_C_STATUS_OK);

  xproc_c_diagnostics_tracker* tracker = nullptr;
  ASSERT_EQ(xproc_c_diagnostics_tracker_create(obs, &tracker), XPROC_C_STATUS_OK);
  ASSERT_NE(tracker, nullptr);

  ASSERT_EQ(xproc_c_diagnostics_tracker_update(tracker), XPROC_C_STATUS_OK);

  bool alive = false;
  ASSERT_EQ(xproc_c_diagnostics_tracker_producer_alive(tracker, &alive), XPROC_C_STATUS_OK);

  uint64_t ms = 0;
  ASSERT_EQ(xproc_c_diagnostics_tracker_idle_ms(tracker, &ms), XPROC_C_STATUS_OK);

  xproc_c_diagnostics_tracker_destroy(tracker);
  xproc_c_observer_close(obs);
  xproc_c_shm_unlink(path);
}

TEST(CApiSmoke, CTrackerProducerAlive) {
  const char* path = "/xproc_capi_tracker_alive";
  xproc_c_shm_unlink(path);

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512;
  opts.type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(uint32_t);

  xproc_c_producer* prod = nullptr;
  xproc_c_observer* obs = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&opts, &prod), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_observer_open(&opts, &obs), XPROC_C_STATUS_OK);

  xproc_c_diagnostics_tracker* tracker = nullptr;
  ASSERT_EQ(xproc_c_diagnostics_tracker_create(obs, &tracker), XPROC_C_STATUS_OK);

  uint32_t val = 42;
  ASSERT_EQ(xproc_c_producer_send_fixed(prod, &val, sizeof(val)), XPROC_C_STATUS_OK);

  ASSERT_EQ(xproc_c_diagnostics_tracker_update(tracker), XPROC_C_STATUS_OK);

  bool alive = false;
  ASSERT_EQ(xproc_c_diagnostics_tracker_producer_alive(tracker, &alive), XPROC_C_STATUS_OK);
  EXPECT_TRUE(alive);

  xproc_c_diagnostics_tracker_destroy(tracker);
  xproc_c_observer_close(obs);
  xproc_c_producer_close(prod);
  xproc_c_shm_unlink(path);
}
```

- [ ] **Step 2: Build and verify tests fail**

Run: `cmake --build build --target xproc_capi_smoke_tests 2>&1 | tail -20`
Expected: Compile error — `xproc_c_observer_occupancy_ratio` etc. undeclared.

- [ ] **Step 3: Add C API declarations to xproc_c.h**

Add before the `#ifdef __cplusplus` closing brace (before line 452):

```c
/**
 * @brief Returns the ring occupancy ratio visible to an observer.
 *
 * @param observer Observer handle.
 * @param out Receives the occupancy ratio in [0.0, 1.0].
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_observer_occupancy_ratio(xproc_c_observer* observer, double* out);

/**
 * @brief Returns the number of occupied bytes in the ring.
 *
 * @param observer Observer handle.
 * @param out Receives the occupied byte count.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_observer_occupancy_bytes(xproc_c_observer* observer, uint64_t* out);

/**
 * @brief Returns the number of available bytes in the ring.
 *
 * @param observer Observer handle.
 * @param out Receives the available byte count.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_observer_available_bytes(xproc_c_observer* observer, uint64_t* out);

/**
 * @brief Returns the consumer lag in bytes (written but not yet consumed).
 *
 * @param observer Observer handle.
 * @param out Receives the lag in bytes.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_observer_consumer_lag_bytes(xproc_c_observer* observer, uint64_t* out);

typedef struct xproc_c_diagnostics_tracker xproc_c_diagnostics_tracker;

/**
 * @brief Creates a diagnostics tracker from an observer's initial state.
 *
 * @param observer Observer handle.
 * @param out Receives the created tracker handle.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_diagnostics_tracker_create(xproc_c_observer* observer,
                                                               xproc_c_diagnostics_tracker** out);

/**
 * @brief Updates the tracker with the observer's current snapshot.
 *
 * @param tracker Tracker handle.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_diagnostics_tracker_update(xproc_c_diagnostics_tracker* tracker);

/**
 * @brief Returns whether the producer has committed new messages since the last update.
 *
 * @param tracker Tracker handle.
 * @param out Receives true if commit_seq changed since previous update.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_diagnostics_tracker_producer_alive(xproc_c_diagnostics_tracker* tracker,
                                                                       bool* out);

/**
 * @brief Returns milliseconds since the last ring progress (write_pos or read_pos change).
 *
 * @param tracker Tracker handle.
 * @param out Receives idle duration in milliseconds.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_diagnostics_tracker_idle_ms(xproc_c_diagnostics_tracker* tracker, uint64_t* out);

/**
 * @brief Destroys a diagnostics tracker handle.
 *
 * Passing NULL is allowed.
 *
 * @param tracker Tracker handle to destroy.
 */
XPROC_C_API void xproc_c_diagnostics_tracker_destroy(xproc_c_diagnostics_tracker* tracker);
```

- [ ] **Step 4: Add C API implementations to xproc_c.cpp**

Add the opaque struct and include at the top of `xproc_c.cpp` (after line 34):

```cpp
#include <xproc/ipc/diagnostics_tracker.hpp>

struct xproc_c_diagnostics_tracker {
  std::unique_ptr<xproc::ipc::diagnostics_tracker> impl;
  xproc_c_observer* owner;  // borrows observer for snapshot updates
};
```

Add implementations after `xproc_c_observer_peek_copy` (after line 749):

```cpp
xproc_c_status xproc_c_observer_occupancy_ratio(xproc_c_observer* observer, double* out) {
  if (out == nullptr) {
    return invalid_argument("xproc_c_observer_occupancy_ratio: out must not be null");
  }
  const xproc_c_status status = validate_handle(observer, "xproc_c_observer_occupancy_ratio: observer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    *out = observer->impl->occupancy_ratio();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_observer_occupancy_bytes(xproc_c_observer* observer, uint64_t* out) {
  if (out == nullptr) {
    return invalid_argument("xproc_c_observer_occupancy_bytes: out must not be null");
  }
  const xproc_c_status status = validate_handle(observer, "xproc_c_observer_occupancy_bytes: observer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    *out = observer->impl->occupancy_bytes();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_observer_available_bytes(xproc_c_observer* observer, uint64_t* out) {
  if (out == nullptr) {
    return invalid_argument("xproc_c_observer_available_bytes: out must not be null");
  }
  const xproc_c_status status = validate_handle(observer, "xproc_c_observer_available_bytes: observer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    *out = observer->impl->available_bytes();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_observer_consumer_lag_bytes(xproc_c_observer* observer, uint64_t* out) {
  if (out == nullptr) {
    return invalid_argument("xproc_c_observer_consumer_lag_bytes: out must not be null");
  }
  const xproc_c_status status = validate_handle(observer, "xproc_c_observer_consumer_lag_bytes: observer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    *out = observer->impl->consumer_lag_bytes();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_diagnostics_tracker_create(xproc_c_observer* observer,
                                                   xproc_c_diagnostics_tracker** out) {
  if (out == nullptr) {
    return invalid_argument("xproc_c_diagnostics_tracker_create: out must not be null");
  }
  *out = nullptr;
  const xproc_c_status status = validate_handle(observer, "xproc_c_diagnostics_tracker_create: observer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    auto handle = std::make_unique<xproc_c_diagnostics_tracker>();
    const auto snap = observer->impl->snapshot();
    const auto cap = static_cast<std::uint64_t>(observer->impl->header()->data_capacity);
    handle->impl = std::make_unique<xproc::ipc::diagnostics_tracker>(snap, cap);
    handle->owner = observer;
    *out = handle.release();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_diagnostics_tracker_update(xproc_c_diagnostics_tracker* tracker) {
  const xproc_c_status status = validate_handle(tracker, "xproc_c_diagnostics_tracker_update: tracker is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    tracker->impl->update(tracker->owner->impl->snapshot());
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_diagnostics_tracker_producer_alive(xproc_c_diagnostics_tracker* tracker, bool* out) {
  if (out == nullptr) {
    return invalid_argument("xproc_c_diagnostics_tracker_producer_alive: out must not be null");
  }
  const xproc_c_status status = validate_handle(tracker, "xproc_c_diagnostics_tracker_producer_alive: tracker is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    *out = tracker->impl->producer_alive();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_diagnostics_tracker_idle_ms(xproc_c_diagnostics_tracker* tracker, uint64_t* out) {
  if (out == nullptr) {
    return invalid_argument("xproc_c_diagnostics_tracker_idle_ms: out must not be null");
  }
  const xproc_c_status status = validate_handle(tracker, "xproc_c_diagnostics_tracker_idle_ms: tracker is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  return catch_status([&]() -> xproc_c_status {
    *out = tracker->impl->idle_duration_ms();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

void xproc_c_diagnostics_tracker_destroy(xproc_c_diagnostics_tracker* tracker) { delete tracker; }
```

- [ ] **Step 5: Build and run all tests**

Run: `cmake --build build --target xproc_capi_smoke_tests xproc_ipc_diagnostics_tests && cd build && ctest -R "capi_smoke|ipc_diagnostics" --output-on-failure`
Expected: All tests PASS (14 C++ diagnostics + 6 new C API smoke tests).

- [ ] **Step 6: Commit**

```bash
git add capi/xproc_c.h capi/xproc_c.cpp tests/capi_smoke_test.cpp
git commit -m "feat: add observer diagnostics to C API

Expose occupancy_ratio, occupancy_bytes, available_bytes,
consumer_lag_bytes as observer-handle functions. Add opaque
diagnostics_tracker handle for producer liveness and idle tracking.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Update phase 2 reference

**Files:**
- Modify: `docs/superpowers/reference/2026-05-28-phase2-reference-design.md:163-168` (mark P3 checklist done)

- [ ] **Step 1: Mark P3 Observer diagnostics checklist as done**

In the phase 2 reference design, change:

```markdown
### P3: Observer diagnostics

- [ ] `occupancy_ratio()` and `occupancy_bytes()` helpers exist
- [ ] `consumer_lag_bytes()` helper exists
- [ ] `producer_liveness()` helper exists
- [ ] Helpers are exposed through C API and at least one binding
```

to:

```markdown
### P3: Observer diagnostics

- [x] `occupancy_ratio()` and `occupancy_bytes()` helpers exist
- [x] `consumer_lag_bytes()` helper exists
- [x] `producer_liveness()` helper exists
- [x] Helpers are exposed through C API and at least one binding
```

Also update the P3 section header to add DONE marker:

```markdown
### P3: Observer / Inspector Diagnostic Helpers -- DONE (2026-06-02)
```

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/reference/2026-05-28-phase2-reference-design.md
git commit -m "docs: mark P3 observer diagnostics as DONE

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
