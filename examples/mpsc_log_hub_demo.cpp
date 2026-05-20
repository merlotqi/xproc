// MPSC - Convergence / fan-in pattern
//
// Typical use cases:
//  - Centralized logging ingestion hub
//  - UI main thread event serialization
//  - Database write buffer coalescing
//  - Metrics aggregation sinks
//
// This is a very common production pattern: multiple producers contend only
// on an in-process mutex queue. A single dedicated hub thread then owns the
// xproc ipc::producer, maintaining the SPSC single-writer guarantee for
// maximum throughput on the shared memory channel.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {
constexpr int kServiceThreads = 5;
constexpr int kLinesPerService = 12;
constexpr std::uint32_t kLineCap = 128;  // fixed slot includes payload + NUL padding in demo

std::string make_line(int svc, int seq) { return "svc" + std::to_string(svc) + " seq=" + std::to_string(seq); }
}  // namespace

int main() {
  std::cout << "=== mpsc_log_hub_demo ===\n"
            << "MPSC convergence / fan-in pattern\n"
            << "Use case: logging hub / event serialization / write buffer coalescing\n"
            << "Pattern: Multiple producers → in-memory mutex queue → single hub thread → SPSC channel\n"
            << "Note: xproc ring remains strict SPSC for maximum throughput; MPSC is only used\n"
            << "      for front-end queueing before the transport layer.\n\n";

  const std::string shm_path = "/xproc_log_hub_" + std::to_string(static_cast<long long>(xproc::current_process_id()));

  // Ensure clean shared memory state
  xproc::shm::shm::unlink(shm_path);

  xproc::ipc::transport_options channel_opts;
  channel_opts.path = shm_path;
  channel_opts.shm_size = sizeof(xproc::shm::control_block) + 512 * 1024;
  channel_opts.type = xproc::ipc::channel_type::fixed;
  channel_opts.item_size = kLineCap;
  channel_opts.create_if_missing = true;

  std::cout << "--- Channel configuration ---\n"
            << "  Shared memory path:  " << shm_path << "\n"
            << "  Fixed slot size:     " << kLineCap << " bytes\n"
            << "  Producer threads:    " << kServiceThreads << "\n"
            << "  Lines per producer:  " << kLinesPerService << "\n\n";

  xproc::ipc::producer producer(channel_opts);
  xproc::ipc::consumer consumer(channel_opts);

  std::mutex q_mu;
  std::condition_variable q_cv;
  std::queue<std::string> pending;
  std::atomic<bool> feeders_done{false};

  // Hub thread: single owner of the producer channel
  // This thread is the only writer to the SPSC ring buffer
  std::thread hub_thread([&] {
    for (;;) {
      std::string log_line;

      {
        std::unique_lock<std::mutex> lock(q_mu);
        q_cv.wait(lock, [&] { return !pending.empty() || feeders_done.load(std::memory_order_acquire); });

        // Exit condition: queue empty and all producers finished
        if (pending.empty() && feeders_done.load(std::memory_order_acquire)) {
          break;
        }

        if (pending.empty()) {
          continue;
        }

        log_line = std::move(pending.front());
        pending.pop();
      }

      q_cv.notify_one();

      // Truncate long lines to fit fixed slot size
      if (log_line.size() + 1 > kLineCap) {
        log_line.resize(static_cast<std::size_t>(kLineCap - 1), '.');
      }

      producer.send_fixed_bytes(log_line.data(), static_cast<std::uint32_t>(log_line.size()));
    }
  });

  std::vector<std::thread> services;
  for (int s = 0; s < kServiceThreads; ++s) {
    services.emplace_back([&, s] {
      for (int i = 0; i < kLinesPerService; ++i) {
        const std::string line = make_line(s, i);
        {
          std::lock_guard<std::mutex> lock(q_mu);
          pending.push(line);
        }
        q_cv.notify_one();
      }
    });
  }

  // Wait for all producer threads to complete
  for (auto& thread : services) {
    thread.join();
  }

  std::cout << "✓ All producer threads completed\n";
  std::cout << "  Draining remaining queue entries...\n";

  // Signal hub thread that no more entries will arrive
  feeders_done.store(true, std::memory_order_release);
  q_cv.notify_one();

  // Wait for hub to finish flushing all entries
  hub_thread.join();

  const int total_lines = kServiceThreads * kLinesPerService;
  std::cout << "✓ Hub thread completed, " << total_lines << " lines forwarded to channel\n\n";

  std::cout << "--- Consumed log lines ---\n";

  int received = 0;
  while (received < total_lines) {
    const bool has_data = consumer.poll([&](void* data, std::uint32_t length) {
      if (length != kLineCap) {
        return;
      }

      const char* line_data = static_cast<const char*>(data);
      const std::size_t line_length = ::strnlen(line_data, static_cast<std::size_t>(length));

      std::cout << "  " << std::string(line_data, line_length) << "\n";
      ++received;
    });

    if (!has_data) {
      // Efficient wait using atomic futex (no busy polling)
      const std::uint32_t current_seq = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, current_seq);
    }
  }

  std::cout << "\n✓ Received " << received << " / " << total_lines << " log lines\n";
  std::cout << "✓ mpsc_log_hub_demo completed successfully\n";
  std::cout << "  Demonstrated: MPSC front queue + SPSC high throughput transport pattern\n";

  // Cleanup shared memory
  xproc::shm::shm::unlink(shm_path);
  return 0;
}
