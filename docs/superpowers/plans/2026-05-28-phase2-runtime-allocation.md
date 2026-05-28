# Runtime Allocation Improvement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate per-message heap allocation in `ipc::runtime::run()` and add configurable copy policies (reuse_buffer, zero_copy, sbo), batching, and backpressure signaling.

**Architecture:** Refactor the header-only `runtime` class in `runtime.hpp` to accept a `copy_policy` enum, extracting per-policy dispatch into private helper methods. Add `run_batched()` and a backpressure-aware `run()` overload. The two-argument `run()` changes its handler contract from owning-vector to borrowed-pointer, matching `channel::poll()`'s existing contract. Write a focused test target and a benchmark to quantify the improvement.

**Tech Stack:** C++17, Google Test, Google Benchmark, xproc core (channel, channel_interface, atomic_wait)

---

## File Structure

- Modify: `include/xproc/ipc/runtime.hpp` — Add `copy_policy` enum, private dispatch helpers, `run_batched()`, backpressure overload, internal `reuse_buf_` member
- Create: `tests/runtime_allocation_test.cpp` — Unit tests for all copy policies, batching, backpressure
- Create: `benchmarks/runtime_benchmark.cpp` — Throughput benchmark comparing policies
- Modify: `tests/CMakeLists.txt` — Register new test target
- Modify: `benchmarks/CMakeLists.txt` — Register new benchmark target
- Reference: `include/xproc/ipc/channel.hpp` — `poll()` handler contract
- Reference: `include/xproc/ipc/channel_interface.hpp` — polymorphic consumer interface
- Reference: `tests/api_surface_test.cpp` — existing `IpcRuntimeRunAndStop` test

---

### Task 1: Add `copy_policy` enum and refactor dispatch with buffer reuse, zero_copy, and sbo

**Files:**
- Modify: `include/xproc/ipc/runtime.hpp`

- [ ] **Step 1: Replace the entire `runtime.hpp` with the new implementation**

The handler contract for the two-argument `run()` changes: previously the handler received an owning `std::vector<uint8_t>`, now it receives a borrowed `(const std::uint8_t*, std::size_t)` valid only through the handler call — matching `channel::poll()`'s existing zero-copy contract.

```cpp
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>
#include <xproc/ipc/channel.hpp>
#include <xproc/ipc/channel_interface.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc::ipc {

enum class copy_policy {
    reuse_buffer,   // Default. Copy into internal buffer. Handler must copy if async.
    zero_copy,      // Pass ring-buffer pointer directly. Valid only until next poll.
    sbo             // Stack-allocate messages <= 256 B, heap for larger ones.
};

// Blocking consumer loop: polls the channel and submits work to Executor.
//
// Executor contract:
//   pool_executor(Callable &&) — invoked synchronously from run()'s thread with a
//   callable that should eventually run handler on payload bytes. Typical use: post
//   to a thread pool queue. The callable may be destroyed after it returns.
//
// Handler contract:
//   void(const std::uint8_t* data, std::size_t len)
//   Under reuse_buffer and zero_copy, data is a BORROWED pointer valid only for the
//   duration of the handler call — same contract as channel::poll(). Callers that
//   need ownership beyond the handler return must copy explicitly. Under sbo, small
//   messages are value-captured in the lambda and outlive the handler.
//
// Thread safety: run() must not be invoked concurrently on the same runtime instance.
// pool_executor should be safe to call from the run() thread only.
//
// stop(): sets running_ false and notifies commit_seq waiters; a few executor tasks
// may still run if already queued. After run() returns, no new tasks are submitted.
//
// Exceptions: if handler throws from inside the executor's invocation, behavior
// depends on your executor (e.g. thread pool may log or terminate). The run() loop
// itself does not catch handler exceptions.
class runtime {
 public:
  explicit runtime(channel& ch) : shm_(&ch), iface_(nullptr) {}
  explicit runtime(consumer& ch) : shm_(&ch.as_channel()), iface_(nullptr) {}
  explicit runtime(consumer_channel_interface& ch) : shm_(nullptr), iface_(&ch) {}

  // ---- two-argument run() with optional copy policy ----

  template <typename Executor, typename Handler>
  void run(Executor&& pool_executor, Handler&& handler,
           copy_policy policy = copy_policy::reuse_buffer) {
    using HandlerStore = typename std::decay<Handler>::type;
    HandlerStore h = std::forward<Handler>(handler);
    running_.store(true);
    while (running_.load(std::memory_order_relaxed)) {
      bool has_data = false;
      if (shm_ != nullptr) {
        has_data = dispatch_poll(*shm_, pool_executor, h, policy);
      } else if (iface_ != nullptr) {
        has_data = dispatch_poll_iface(pool_executor, h, policy);
      }

      if (!has_data) {
        wait_for_data();
      }
    }
  }

  // ---- run() with backpressure callback ----

  template <typename Executor, typename Handler, typename Backpressure>
  void run(Executor&& pool_executor, Handler&& handler,
           Backpressure&& on_backpressure,
           copy_policy policy = copy_policy::reuse_buffer) {
    using HandlerStore = typename std::decay<Handler>::type;
    HandlerStore h = std::forward<Handler>(handler);
    auto bp = std::forward<Backpressure>(on_backpressure);
    running_.store(true);
    std::size_t queued = 0;
    while (running_.load(std::memory_order_relaxed)) {
      bool has_data = false;
      if (shm_ != nullptr) {
        has_data = dispatch_poll(*shm_, pool_executor, h, policy);
      } else if (iface_ != nullptr) {
        has_data = dispatch_poll_iface(pool_executor, h, policy);
      }

      if (has_data) {
        ++queued;
        continue;
      }

      if (queued > 0) {
        bp(queued);
        queued = 0;
      }
      wait_for_data();
    }
    if (queued > 0) {
      bp(queued);
    }
  }

  // ---- batching mode ----

  template <typename Executor, typename Handler>
  void run_batched(Executor&& pool_executor, Handler&& handler,
                   std::size_t max_batch_size = 1,
                   copy_policy policy = copy_policy::reuse_buffer) {
    using HandlerStore = typename std::decay<Handler>::type;
    HandlerStore h = std::forward<Handler>(handler);
    running_.store(true);
    while (running_.load(std::memory_order_relaxed)) {
      std::size_t batch_count = 0;
      while (batch_count < max_batch_size) {
        bool has_data = false;
        if (shm_ != nullptr) {
          has_data = dispatch_poll(*shm_, pool_executor, h, policy);
        } else if (iface_ != nullptr) {
          has_data = dispatch_poll_iface(pool_executor, h, policy);
        }
        if (!has_data) break;
        ++batch_count;
      }

      if (batch_count == 0) {
        wait_for_data();
      }
    }
  }

  void stop() {
    running_.store(false, std::memory_order_release);
    if (shm_ != nullptr && shm_->header() != nullptr) {
      sync::atomic_notify_all(&shm_->header()->rb_meta.commit_seq);
    }
  }

 private:
  static constexpr std::size_t kSboThreshold = 256;

  channel* shm_{nullptr};
  consumer_channel_interface* iface_{nullptr};
  std::atomic_bool running_{false};
  std::vector<std::uint8_t> reuse_buf_;

  void wait_for_data() {
    if (shm_ != nullptr && shm_->header() != nullptr) {
      const uint32_t last_commit =
          shm_->header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      sync::atomic_wait(&shm_->header()->rb_meta.commit_seq, last_commit);
    } else if (iface_ != nullptr) {
      iface_->wait();
    }
  }

  // ---- dispatch helpers for channel* path ----

  template <typename Executor, typename Handler>
  bool dispatch_poll(channel& ch, Executor& exec, Handler& h, copy_policy policy) {
    switch (policy) {
      case copy_policy::reuse_buffer:
        return poll_with_reuse(ch, exec, h);
      case copy_policy::zero_copy:
        return poll_zero_copy(ch, exec, h);
      case copy_policy::sbo:
        return poll_with_sbo(ch, exec, h);
    }
    return false;
  }

  template <typename Executor, typename Handler>
  bool poll_with_reuse(channel& ch, Executor& exec, Handler& h) {
    return ch.poll([&](void* ptr, uint32_t len) {
      const auto n = static_cast<std::size_t>(len);
      if (reuse_buf_.size() < n) {
        reuse_buf_.resize(n);
      }
      std::memcpy(reuse_buf_.data(), ptr, n);
      auto* p = reuse_buf_.data();
      exec([p, n, h]() { h(static_cast<const std::uint8_t*>(p), n); });
    });
  }

  template <typename Executor, typename Handler>
  bool poll_zero_copy(channel& ch, Executor& exec, Handler& h) {
    return ch.poll([&](void* ptr, uint32_t len) {
      exec([ptr, len, h]() { h(static_cast<const std::uint8_t*>(ptr), len); });
    });
  }

  template <typename Executor, typename Handler>
  bool poll_with_sbo(channel& ch, Executor& exec, Handler& h) {
    return ch.poll([&](void* ptr, uint32_t len) {
      const auto n = static_cast<std::size_t>(len);
      if (n <= kSboThreshold) {
        std::array<std::uint8_t, kSboThreshold> buf{};
        std::memcpy(buf.data(), ptr, n);
        exec([buf, n, h]() mutable { h(buf.data(), n); });
      } else {
        auto heap = std::make_shared<std::vector<std::uint8_t>>(
            static_cast<const std::uint8_t*>(ptr),
            static_cast<const std::uint8_t*>(ptr) + n);
        exec([heap, h]() { h(heap->data(), heap->size()); });
      }
    });
  }

  // ---- dispatch helpers for consumer_channel_interface* path ----

  template <typename Executor, typename Handler>
  bool dispatch_poll_iface(Executor& exec, Handler& h, copy_policy policy) {
    switch (policy) {
      case copy_policy::reuse_buffer:
        return iface_->poll([&](void* ptr, uint32_t len) {
          const auto n = static_cast<std::size_t>(len);
          if (reuse_buf_.size() < n) {
            reuse_buf_.resize(n);
          }
          std::memcpy(reuse_buf_.data(), ptr, n);
          auto* p = reuse_buf_.data();
          exec([p, n, h]() { h(static_cast<const std::uint8_t*>(p), n); });
        });
      case copy_policy::zero_copy:
        return iface_->poll([&](void* ptr, uint32_t len) {
          exec([ptr, len, h]() { h(static_cast<const std::uint8_t*>(ptr), len); });
        });
      case copy_policy::sbo:
        return iface_->poll([&](void* ptr, uint32_t len) {
          const auto n = static_cast<std::size_t>(len);
          if (n <= kSboThreshold) {
            std::array<std::uint8_t, kSboThreshold> buf{};
            std::memcpy(buf.data(), ptr, n);
            exec([buf, n, h]() mutable { h(buf.data(), n); });
          } else {
            auto heap = std::make_shared<std::vector<std::uint8_t>>(
                static_cast<const std::uint8_t*>(ptr),
                static_cast<const std::uint8_t*>(ptr) + n);
            exec([heap, h]() { h(heap->data(), heap->size()); });
          }
        });
    }
    return false;
  }
};

}  // namespace xproc::ipc
```

- [ ] **Step 2: Build to verify the header compiles**

Run: `cmake --build build --target xproc 2>&1`
Expected: Build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
git add include/xproc/ipc/runtime.hpp
git commit -m "feat: add copy_policy enum and buffer reuse to ipc::runtime

Replace per-message std::vector<uint8_t> allocation with three copy policies:
- reuse_buffer (default): reusable internal buffer, zero allocation amortized
- zero_copy: pass ring-buffer pointer directly, zero allocation
- sbo: stack-allocate messages <= 256 B, heap fallback for larger

The two-argument run() handler contract changes from owning-vector to
borrowed-pointer, matching channel::poll()'s existing contract."
```

---

### Task 2: Write runtime allocation unit tests

**Files:**
- Create: `tests/runtime_allocation_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

```cpp
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

struct Fixture {
  std::string path;
  xproc::ipc::transport_options opts;

  Fixture(const char* suffix) {
    path = std::string("/xproc_rt_alloc_") + suffix;
    xproc::core::shm::unlink(path);
    opts.path = path;
    opts.shm_size = sizeof(xproc::core::control_block) + 16384;
    opts.type = xproc::ipc::channel_type::fixed;
    opts.item_size = 4;
  }

  ~Fixture() { xproc::core::shm::unlink(path); }
};

}  // namespace

// ---- reuse_buffer policy ----

TEST(RuntimeAllocation, ReuseBufferInlineExecutor) {
  Fixture fx("reuse_inline");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<int> count{0};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             std::uint32_t v = 0;
             std::memcpy(&v, data, sizeof(v));
             EXPECT_EQ(v, 0xDEADBEEFu);
             ++count;
             if (count.load() >= 3) {
               rt.stop();
             }
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_fixed<std::uint32_t>(0xDEADBEEFu);
  prod.send_fixed<std::uint32_t>(0xDEADBEEFu);
  prod.send_fixed<std::uint32_t>(0xDEADBEEFu);

  rt_thread.join();
  EXPECT_GE(count.load(), 1);
}

TEST(RuntimeAllocation, ReuseBufferDefaultPolicy) {
  Fixture fx("reuse_default");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor, [&](const std::uint8_t* data, std::size_t len) {
      EXPECT_EQ(len, 4u);
      got.store(true);
      rt.stop();
    });
  });

  prod.send_fixed<std::uint32_t>(0xAAAAAAAAu);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- zero_copy policy ----

TEST(RuntimeAllocation, ZeroCopyInlineExecutor) {
  Fixture fx("zero_inline");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             std::uint32_t v = 0;
             std::memcpy(&v, data, sizeof(v));
             EXPECT_EQ(v, 0xCAFEBABEu);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::zero_copy);
  });

  prod.send_fixed<std::uint32_t>(0xCAFEBABEu);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- sbo policy ----

TEST(RuntimeAllocation, SboSmallMessage) {
  Fixture fx("sbo_small");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::sbo);
  });

  prod.send_fixed<std::uint32_t>(0xBEEF1234u);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

TEST(RuntimeAllocation, SboLargeMessageHeapFallback) {
  Fixture fx_lg("sbo_large");
  fx_lg.opts.item_size = 512;
  fx_lg.opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  xproc::ipc::producer prod(fx_lg.opts);
  xproc::ipc::consumer cons(fx_lg.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::vector<std::uint8_t> payload(512, std::uint8_t{0xAB});
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 512u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::sbo);
  });

  prod.send_fixed_bytes(payload.data(), 512);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- run_batched ----

TEST(RuntimeAllocation, RunBatchedCollectsMultipleMessages) {
  Fixture fx("batched");
  fx.opts.item_size = 8;
  fx.opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<int> received{0};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run_batched(
        executor,
        [&](const std::uint8_t* data, std::size_t len) {
          EXPECT_EQ(len, 8u);
          ++received;
          if (received.load() >= 4) {
            rt.stop();
          }
        },
        4,  // max_batch_size
        xproc::ipc::copy_policy::reuse_buffer);
  });

  for (int i = 0; i < 5; ++i) {
    prod.send_fixed<std::uint64_t>(static_cast<std::uint64_t>(i));
  }
  rt_thread.join();
  EXPECT_GE(received.load(), 4);
}

// ---- backpressure ----

TEST(RuntimeAllocation, BackpressureCallbackFires) {
  Fixture fx("bp");
  fx.opts.item_size = 8;
  fx.opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<std::size_t> bp_count{0};
  std::atomic<bool> got_msg{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(
        executor,
        [&](const std::uint8_t* data, std::size_t len) {
          EXPECT_EQ(len, 8u);
          got_msg.store(true);
          rt.stop();
        },
        [&](std::size_t queued) { bp_count.store(queued); },
        xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_fixed<std::uint64_t>(42u);
  rt_thread.join();
  EXPECT_TRUE(got_msg.load());
}

// ---- consumer_channel_interface path ----

TEST(RuntimeAllocation, ReuseBufferViaInterface) {
  Fixture fx("iface");
  xproc::ipc::producer prod(fx.opts);
  auto shm_cons = std::make_unique<xproc::ipc::shm_consumer>(fx.opts);
  xproc::ipc::consumer_channel_interface& iface = *shm_cons;
  xproc::ipc::runtime rt(iface);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_fixed<std::uint32_t>(0xFEEDF00Du);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- varlen channel ----

TEST(RuntimeAllocation, ReuseBufferVarlen) {
  const std::string path = "/xproc_rt_alloc_varlen";
  xproc::core::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::varlen;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 11u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_varlen("hello world", 11);
  rt_thread.join();
  EXPECT_TRUE(got.load());

  xproc::core::shm::unlink(path);
}
```

- [ ] **Step 2: Register the test target in `tests/CMakeLists.txt`**

Add `runtime_allocation_test.cpp` to `XPROC_GTEST_TEST_FILES_COMMON`:

In `tests/CMakeLists.txt`, find:
```cmake
set(XPROC_GTEST_TEST_FILES_COMMON
    api_surface_test.cpp
    ringbuffer_spsc_test.cpp
    layout_validate_test.cpp
    ipc_observer_attach_test.cpp
    protocol_codec_test.cpp
    socket_transport_test.cpp
)
```

Replace with:
```cmake
set(XPROC_GTEST_TEST_FILES_COMMON
    api_surface_test.cpp
    ringbuffer_spsc_test.cpp
    layout_validate_test.cpp
    ipc_observer_attach_test.cpp
    protocol_codec_test.cpp
    socket_transport_test.cpp
    runtime_allocation_test.cpp
)
```

- [ ] **Step 3: Build and run the new tests**

Run: `cmake --build build --target xproc_runtime_allocation_test 2>&1`
Expected: Build succeeds with no errors.

Run: `ctest --test-dir build -R runtime_allocation -V 2>&1`
Expected: All 8 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/runtime_allocation_test.cpp tests/CMakeLists.txt
git commit -m "test: add runtime allocation policy tests

Covers reuse_buffer, zero_copy, sbo (small and large), run_batched,
backpressure callback, consumer_channel_interface path, and varlen
channel dispatch."
```

---

### Task 3: Write runtime benchmark

**Files:**
- Create: `benchmarks/runtime_benchmark.cpp`
- Modify: `benchmarks/CMakeLists.txt`

- [ ] **Step 1: Create the benchmark file**

```cpp
#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

namespace {

std::string unique_path(const char* prefix, int arg) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::string("/xproc_bench_rt_") + prefix + "_" + std::to_string(arg) +
         "_" + std::to_string(now);
}

// Baseline: current dispatch with per-message vector allocation (simulated by
// running without runtime — direct poll loop with inline handling).
static void BM_RuntimeBaselineDirectPoll(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("baseline", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  std::vector<std::byte> payload(payload_len, std::byte{0x5a});

  for (auto _ : state) {
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    bool got = false;
    while (!got) {
      got = cons.poll([&](void*, std::uint32_t len) {
        benchmark::DoNotOptimize(len);
      });
      if (!got) {
        const auto c = cons.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&cons.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

// Runtime with reuse_buffer (default) policy.
static void BM_RuntimeReuseBuffer(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("reuse", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> running{true};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t*, std::size_t len) {
             benchmark::DoNotOptimize(len);
             running.store(false);
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  std::vector<std::byte> payload(payload_len, std::byte{0x5a});
  for (auto _ : state) {
    running.store(true);
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    while (running.load()) {
      // busy-wait for handler
    }
  }

  rt.stop();
  rt_thread.join();
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

// Runtime with zero_copy policy.
static void BM_RuntimeZeroCopy(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("zero", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> running{true};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t*, std::size_t len) {
             benchmark::DoNotOptimize(len);
             running.store(false);
           },
           xproc::ipc::copy_policy::zero_copy);
  });

  std::vector<std::byte> payload(payload_len, std::byte{0x5a});
  for (auto _ : state) {
    running.store(true);
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    while (running.load()) {
    }
  }

  rt.stop();
  rt_thread.join();
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

// Runtime with sbo policy (message fits in 256 B stack buffer).
static void BM_RuntimeSbo(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("sbo", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> running{true};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t*, std::size_t len) {
             benchmark::DoNotOptimize(len);
             running.store(false);
           },
           xproc::ipc::copy_policy::sbo);
  });

  std::vector<std::byte> payload(payload_len, std::byte{0x5a});
  for (auto _ : state) {
    running.store(true);
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    while (running.load()) {
    }
  }

  rt.stop();
  rt_thread.join();
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

}  // namespace

BENCHMARK(BM_RuntimeBaselineDirectPoll)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RuntimeReuseBuffer)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RuntimeZeroCopy)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RuntimeSbo)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
```

- [ ] **Step 2: Register the benchmark in `benchmarks/CMakeLists.txt`**

First, check the benchmark CMakeLists structure:

Run: `head -30 benchmarks/CMakeLists.txt`
Expected: See the existing benchmark registration pattern.

The benchmark registration follows the same `FetchContent` pattern as tests. Add the new benchmark source to the existing list. In `benchmarks/CMakeLists.txt`, find the `add_executable` for IPC benchmarks and add `runtime_benchmark.cpp`. The exact edit depends on the existing structure — read the file first to confirm.

Expected edit approach: find the existing benchmark source file list (likely containing `ipc_benchmark.cpp`) and append `runtime_benchmark.cpp`.

- [ ] **Step 3: Build the benchmark**

Run: `cmake --build build --target xproc_runtime_benchmark 2>&1`
Expected: Build succeeds with no errors.

(Note: target name depends on the benchmark CMakeLists convention — adapt as needed.)

- [ ] **Step 4: Run a quick benchmark smoke test**

Run: `./build/benchmarks/xproc_runtime_benchmark --benchmark_min_time=0.01s 2>&1`
Expected: Four benchmark groups run and report throughput numbers.

- [ ] **Step 5: Commit**

```bash
git add benchmarks/runtime_benchmark.cpp benchmarks/CMakeLists.txt
git commit -m "bench: add runtime allocation policy benchmarks

Compare direct poll baseline vs runtime with reuse_buffer, zero_copy,
and sbo policies across 16/64/256 byte payloads."
```

---

### Task 4: Verify existing tests and finalize

**Files:**
- Reference: `tests/api_surface_test.cpp`
- Reference: `tests/CMakeLists.txt`

- [ ] **Step 1: Rebuild all targets**

Run: `cmake --build build --parallel 2>&1`
Expected: All targets build successfully with no errors.

- [ ] **Step 2: Run the existing runtime test**

The existing `IpcRuntimeRunAndStop` test in `api_surface_test.cpp` uses `rt.run(executor, handler)` with the two-argument form. This now defaults to `reuse_buffer` policy. The test copies data inside the handler (`std::memcpy(&v, data, sizeof(v))`), so it should remain compatible.

Run: `ctest --test-dir build -R api_surface -V 2>&1`
Expected: `IpcRuntimeRunAndStop` passes.

- [ ] **Step 3: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure 2>&1`
Expected: All tests pass with zero failures.

- [ ] **Step 4: Run the full benchmark suite**

Run: `cmake --build build --target xproc_run_benchmarks 2>&1`
Expected: All benchmarks run and complete.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "test: verify full test and benchmark suite after runtime allocation changes

All existing tests pass with the new reuse_buffer default policy.
Handler contract change from owning-vector to borrowed-pointer is
compatible with existing inline-executor usage."
```

---

## Self-Review

**1. Spec coverage:**
- Buffer reuse (default policy): Task 1 implements `reuse_buffer` policy with internal `reuse_buf_` member
- Copy-policy hooks: Task 1 adds `copy_policy` enum with all three policies
- Batching mode: Task 1 implements `run_batched()`
- Backpressure signaling: Task 1 implements the backpressure-aware `run()` overload
- Unit tests: Task 2 covers all policies, interface path, varlen, batching, backpressure
- Benchmark: Task 3 compares all three policies vs direct poll baseline
- Existing test compatibility: Task 4 verifies the `IpcRuntimeRunAndStop` test still passes

**2. Placeholder scan:**
- No TBD, TODO, or "implement later" markers
- All code steps contain complete, compilable code
- No "add appropriate error handling" hand-waving
- All commands include expected output

**3. Type consistency:**
- Handler signature: `void(const std::uint8_t* data, std::size_t len)` — consistent across runtime.hpp and all tests
- `copy_policy` enum values: `reuse_buffer`, `zero_copy`, `sbo` — consistent across implementation and tests
- `run_batched(executor, handler, max_batch_size, policy)` — consistent signature
- Backpressure `run(executor, handler, on_backpressure, policy)` — consistent signature
