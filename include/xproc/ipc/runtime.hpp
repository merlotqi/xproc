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
  reuse_buffer,  // Default. Copy into internal buffer. Handler must copy if async.
  zero_copy,     // Pass ring-buffer pointer directly. Valid only until next poll.
  sbo            // Stack-allocate messages <= 256 B, heap for larger ones.
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
  void run(Executor&& pool_executor, Handler&& handler, copy_policy policy = copy_policy::reuse_buffer) {
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
  void run(Executor&& pool_executor, Handler&& handler, Backpressure&& on_backpressure,
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
  void run_batched(Executor&& pool_executor, Handler&& handler, std::size_t max_batch_size = 1,
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
      shm_->header()->rb_meta.commit_seq.fetch_add(1, std::memory_order_release);
      sync::atomic_notify_all(&shm_->header()->rb_meta.commit_seq);
    }
    if (iface_ != nullptr) {
      if (auto* header = iface_->shared_header(); header != nullptr) {
        header->rb_meta.commit_seq.fetch_add(1, std::memory_order_release);
        sync::atomic_notify_all(&header->rb_meta.commit_seq);
      }
      iface_->interrupt_wait();
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
      wait_for_shared_header(shm_->header());
    } else if (iface_ != nullptr) {
      if (auto* header = iface_->shared_header(); header != nullptr) {
        wait_for_shared_header(header);
        return;
      }
      if (!running_.load(std::memory_order_acquire)) {
        return;
      }
      iface_->wait();
    }
  }

  void wait_for_shared_header(core::control_block* header) {
    if (header == nullptr) {
      return;
    }
    const uint32_t last_commit = header->rb_meta.commit_seq.load(std::memory_order_acquire);
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    sync::atomic_wait(&header->rb_meta.commit_seq, last_commit);
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
    return ch.poll(
        [&](void* ptr, uint32_t len) { exec([ptr, len, h]() { h(static_cast<const std::uint8_t*>(ptr), len); }); });
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
        auto heap = std::make_shared<std::vector<std::uint8_t>>(static_cast<const std::uint8_t*>(ptr),
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
        return iface_->poll(
            [&](void* ptr, uint32_t len) { exec([ptr, len, h]() { h(static_cast<const std::uint8_t*>(ptr), len); }); });
      case copy_policy::sbo:
        return iface_->poll([&](void* ptr, uint32_t len) {
          const auto n = static_cast<std::size_t>(len);
          if (n <= kSboThreshold) {
            std::array<std::uint8_t, kSboThreshold> buf{};
            std::memcpy(buf.data(), ptr, n);
            exec([buf, n, h]() mutable { h(buf.data(), n); });
          } else {
            auto heap = std::make_shared<std::vector<std::uint8_t>>(static_cast<const std::uint8_t*>(ptr),
                                                                    static_cast<const std::uint8_t*>(ptr) + n);
            exec([heap, h]() { h(heap->data(), heap->size()); });
          }
        });
    }
    return false;
  }
};

}  // namespace xproc::ipc
