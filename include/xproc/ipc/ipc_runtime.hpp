#pragma once

#include <atomic>
#include <type_traits>
#include <vector>
#include <xproc/ipc/ipc_channel.hpp>
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
  explicit ipc_runtime(ipc_channel &channel) : channel_(channel) {}
  explicit ipc_runtime(consumer_channel &channel) : channel_(channel.as_ipc_channel()) {}

  template <typename Executor, typename Handler>
  void run(Executor &&pool_executor, Handler &&handler) {
    using HandlerStore = typename std::decay<Handler>::type;
    HandlerStore h = std::forward<Handler>(handler);
    running_.store(true);
    while (running_.load(std::memory_order_relaxed)) {
      bool has_data = channel_.poll([&](void *ptr, uint32_t len) {
        std::vector<std::uint8_t> copy_data(static_cast<std::uint8_t *>(ptr), static_cast<std::uint8_t *>(ptr) + len);
        pool_executor([data = std::move(copy_data), h]() mutable { h(data.data(), data.size()); });
      });

      if (!has_data) {
        const uint32_t last_commit = channel_.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        sync::atomic_wait(&channel_.header()->rb_meta.commit_seq, last_commit);
      }
    }
  }

  void stop() {
    running_.store(false, std::memory_order_release);
    if (channel_.header() != nullptr) {
      sync::atomic_notify_all(&channel_.header()->rb_meta.commit_seq);
    }
  }

 private:
  ipc_channel &channel_;
  std::atomic_bool running_{false};
};

}  // namespace ipc
}  // namespace xproc
