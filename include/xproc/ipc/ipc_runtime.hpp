#pragma once

#include <atomic>
#include <type_traits>
#include <vector>
#include <xproc/ipc/ipc_channel.hpp>
#include <xproc/ipc/ipc_channel_interface.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc {
namespace ipc {

// Blocking consumer loop: polls the channel, copies each message, and submits work to Executor.
//
// Executor contract:
//   pool_executor(Callable &&) — invoked synchronously from run()'s thread with a callable that
//   should eventually run handler on payload bytes. Typical use: post to a thread pool queue.
//   The callable may be destroyed after it returns; the embedded std::vector<uint8_t> must be
//   moved into the async worker (as implemented below).
//
// Thread safety: run() must not be invoked concurrently on the same ipc_runtime instance.
// pool_executor should be safe to call from the run() thread only (same as poll thread).
//
// stop(): sets running_ false and notifies commit_seq waiters; a few executor tasks may still
// run if already queued. After run() returns, no new tasks are submitted.
//
// Exceptions: if handler throws from inside the executor's invocation of the task, behavior
// depends on your executor (e.g. thread pool may log or terminate). The run() loop itself does
// not catch handler exceptions.
class ipc_runtime {
 public:
  explicit ipc_runtime(ipc_channel& channel) : shm_(&channel), iface_(nullptr) {}
  explicit ipc_runtime(consumer_channel& channel) : shm_(&channel.as_ipc_channel()), iface_(nullptr) {}
  explicit ipc_runtime(IConsumerChannel& channel) : shm_(nullptr), iface_(&channel) {}

  template <typename Executor, typename Handler>
  void run(Executor&& pool_executor, Handler&& handler) {
    using HandlerStore = typename std::decay<Handler>::type;
    HandlerStore h = std::forward<Handler>(handler);
    running_.store(true);
    while (running_.load(std::memory_order_relaxed)) {
      bool has_data = false;
      if (shm_ != nullptr) {
        has_data = shm_->poll([&](void* ptr, uint32_t len) {
          std::vector<std::uint8_t> copy_data(static_cast<std::uint8_t*>(ptr), static_cast<std::uint8_t*>(ptr) + len);
          pool_executor([data = std::move(copy_data), h]() mutable { h(data.data(), data.size()); });
        });
      } else {
        has_data = iface_->poll([&](void* ptr, uint32_t len) {
          std::vector<std::uint8_t> copy_data(static_cast<std::uint8_t*>(ptr), static_cast<std::uint8_t*>(ptr) + len);
          pool_executor([data = std::move(copy_data), h]() mutable { h(data.data(), data.size()); });
        });
      }

      if (!has_data) {
        if (shm_ != nullptr && shm_->header() != nullptr) {
          const uint32_t last_commit = shm_->header()->rb_meta.commit_seq.load(std::memory_order_acquire);
          sync::atomic_wait(&shm_->header()->rb_meta.commit_seq, last_commit);
        } else if (iface_ != nullptr) {
          iface_->wait_when_empty();
        }
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
  ipc_channel* shm_{nullptr};
  IConsumerChannel* iface_{nullptr};
  std::atomic_bool running_{false};
};

}  // namespace ipc
}  // namespace xproc
