// MPMC - Contention / shared work queue pattern
//
// Typical uses: Goroutine scheduler internals, HTTP request thread pools,
// task queues with multiple producers and multiple consumers. Production grade
// systems generally implement lock-free work stealing queues. This demonstration
// uses a single mutex + std::deque ("big lock" approach) to keep the example
// simple, obvious and correctness focused.
//
// xproc is used here for an optional side channel: we attach a lightweight SPSC
// telemetry channel where worker threads can report completion metrics, which
// are then collected by a separate monitoring thread.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {
constexpr int kClientThreads = 4;  // push "HTTP requests" (task ids)
constexpr int kWorkerThreads = 4;  // compete to pop and "handle"
constexpr int kTasksPerClient = 80;
constexpr int kPoolTotal = kClientThreads * kTasksPerClient;

constexpr int kTelemetryMax = 256;  // report at most this many completions over SHM for brevity
}  // namespace

int main() {
  std::cout << "=== mpmc_worker_pool_demo ===\n"
            << "MPMC contention pattern: shared work queue\n"
            << "Use case: scheduler / HTTP worker pool - many threads push & pop shared tasks\n"
            << "Note: Production systems use lock-free work stealing; this demo uses a single\n"
            << "      mutex + deque for clarity and correctness demonstration.\n\n";

  std::mutex pool_mu;
  std::condition_variable pool_cv;
  std::deque<std::uint32_t> pool;
  std::atomic<int> pushed{0};
  std::atomic<int> popped{0};
  std::atomic<bool> pushers_done{false};

  std::cout << "--- Part A: In-process MPMC task pool ---\n"
            << "  Producer threads:   " << kClientThreads << " (each generates " << kTasksPerClient << " tasks)\n"
            << "  Worker threads:     " << kWorkerThreads << " (contend for queue access)\n"
            << "  Total tasks:        " << kPoolTotal << "\n\n";

  std::vector<std::thread> clients;
  for (int c = 0; c < kClientThreads; ++c) {
    clients.emplace_back([&, c] {
      const std::uint32_t base = static_cast<std::uint32_t>(c * kTasksPerClient + 1);
      for (int i = 0; i < kTasksPerClient; ++i) {
        const std::uint32_t id = base + static_cast<std::uint32_t>(i);
        {
          std::lock_guard<std::mutex> lock(pool_mu);
          pool.push_back(id);
        }
        pool_cv.notify_one();
        pushed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::vector<std::thread> workers;
  for (int w = 0; w < kWorkerThreads; ++w) {
    workers.emplace_back([&, w] {
      (void)w;
      for (;;) {
        std::uint32_t task = 0;
        {
          std::unique_lock<std::mutex> lock(pool_mu);
          pool_cv.wait(lock, [&] {
            return !pool.empty() || (pushers_done.load(std::memory_order_acquire) &&
                                     pushed.load(std::memory_order_acquire) == popped.load(std::memory_order_acquire));
          });
          if (pool.empty() && pushers_done.load(std::memory_order_acquire) &&
              pushed.load(std::memory_order_acquire) == popped.load(std::memory_order_acquire)) {
            break;
          }
          if (pool.empty()) {
            continue;
          }
          task = pool.front();
          pool.pop_front();
        }
        pool_cv.notify_all();
        popped.fetch_add(1, std::memory_order_relaxed);
        (void)task;
      }
    });
  }

  for (auto& t : clients) {
    t.join();
  }
  pushers_done.store(true, std::memory_order_release);
  pool_cv.notify_all();

  for (auto& t : workers) {
    t.join();
  }

  // Verify all tasks were processed
  const int total_pushed = pushed.load(std::memory_order_relaxed);
  const int total_popped = popped.load(std::memory_order_relaxed);

  if (total_popped != kPoolTotal || total_pushed != kPoolTotal) {
    std::cerr << "Error: task count mismatch\n"
              << "  Expected:  " << kPoolTotal << "\n"
              << "  Pushed:    " << total_pushed << "\n"
              << "  Processed: " << total_popped << "\n";
    return 1;
  }

  std::cout << "  ✓ All " << kPoolTotal << " tasks processed successfully\n\n";

  // Part B: xproc SPSC telemetry channel
  //
  // This demonstrates a common production pattern: workers do their main work
  // on the contended MPMC pool, but send monitoring metrics via separate
  // lock-free SPSC channels to avoid adding overhead to the critical path.

  const std::string shm_path =
      "/xproc_pool_telemetry_" + std::to_string(static_cast<long long>(xproc::current_process_id()));

  // Ensure clean state before creating channel
  xproc::shm::shm::unlink(shm_path);

  std::cout << "--- Part B: SPSC telemetry channel demonstration ---\n"
            << "  Shared memory path:  " << shm_path << "\n"
            << "  Sample count:        " << kTelemetryMax << " completion events\n\n";

  constexpr std::size_t kTelemetryCapacity = 32 * 1024;
  const auto channel = xproc::ipc::make_fixed_channel(shm_path, sizeof(std::uint32_t)).create(kTelemetryCapacity);
  xproc::ipc::producer producer = channel.open_producer();
  xproc::ipc::consumer consumer = channel.open_consumer();

  // Monitoring sink thread - collects telemetry samples
  std::thread sink_thread([&] {
    int sample_count = 0;

    while (sample_count < kTelemetryMax) {
      const bool received = consumer.poll([&](void* data, std::uint32_t length) {
        if (length != sizeof(std::uint32_t)) {
          return;
        }

        const std::uint32_t task_id = *static_cast<const std::uint32_t*>(data);

        // Print first few and last few samples for demonstration
        if (sample_count < 4 || sample_count >= kTelemetryMax - 2) {
          std::cout << "  [monitor] sample " << sample_count << " task_id=" << task_id << "\n";
        } else if (sample_count == 4) {
          std::cout << "  [monitor] ... (" << (kTelemetryMax - 6) << " samples omitted) ...\n";
        }

        ++sample_count;
      });

      if (!received) {
        // Block on atomic wait until new data arrives (no busy waiting)
        const std::uint32_t current_seq = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, current_seq);
      }
    }
  });

  // Simulate telemetry emission from work loop
  for (int i = 0; i < kTelemetryMax; ++i) {
    producer.send_fixed(static_cast<std::uint32_t>(1000u + static_cast<std::uint32_t>(i)));
  }

  sink_thread.join();

  std::cout << "\n✓ mpmc_worker_pool_demo completed successfully\n";
  std::cout << "  Demonstrated: MPMC contention pattern + SPSC telemetry side channel\n";

  xproc::shm::shm::unlink(shm_path);
  return 0;
}
