// SPMC - Broadcast / fan-out pattern
//
// Typical use cases:
//  - Configuration distribution to service replicas
//  - Audio/Video frame dispatch to decoder workers
//  - Event broadcasting to multiple subscribers
//  - Metric snapshot distribution
//
// Implementation notes:
//  xproc channels are strictly SPSC. To implement fan-out patterns, a dedicated
//  dispatcher thread reads once from the channel, then makes N copies to each
//  subscriber queue. This maintains maximum throughput on the transport layer.
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
constexpr std::uint32_t kSlotBytes = 256;
constexpr int kReplicaWorkers = 3;
constexpr int kConfigRounds = 6;

std::string build_config_snapshot(int revision) {
  return std::string(R"({"rev":)") + std::to_string(revision) +
         R"(,"mode":"broadcast","note":"immutable copy per replica"})";
}
}  // namespace

int main() {
  std::cout << "=== spmc_config_broadcast_demo ===\n"
            << "SPMC broadcast / fan-out pattern\n"
            << "Use case: configuration distribution / AV frame dispatch\n"
            << "Pattern: Single source → SPSC channel → dispatcher thread → N replica queues\n"
            << "Note: xproc channel remains strict SPSC; fan-out is implemented by copying\n"
            << "      in user space for maximum transport performance.\n\n";

  const std::string shm_path =
      "/xproc_config_broadcast_" + std::to_string(static_cast<long long>(xproc::current_process_id()));

  // Clean up any previous instance
  xproc::shm::shm::unlink(shm_path);

  xproc::ipc::transport_options channel_opts;
  channel_opts.path = shm_path;
  channel_opts.shm_size = sizeof(xproc::core::control_block) + 128 * 1024;
  channel_opts.type = xproc::ipc::channel_type::fixed;
  channel_opts.item_size = kSlotBytes;
  channel_opts.create_if_missing = true;

  std::cout << "--- Channel configuration ---\n"
            << "  Shared memory path:  " << shm_path << "\n"
            << "  Slot size:           " << kSlotBytes << " bytes\n"
            << "  Replica workers:     " << kReplicaWorkers << "\n"
            << "  Configuration rounds:" << kConfigRounds << "\n\n";

  xproc::ipc::producer producer(channel_opts);
  xproc::ipc::consumer consumer(channel_opts);

  struct ReplicaQueue {
    std::mutex mu;
    std::condition_variable cv;
    std::queue<std::string> q;
    std::atomic<bool> closed{false};
  };
  std::vector<ReplicaQueue> replicas(static_cast<std::size_t>(kReplicaWorkers));

  std::vector<int> received_per(static_cast<std::size_t>(kReplicaWorkers), 0);
  std::vector<std::thread> workers;

  for (int w = 0; w < kReplicaWorkers; ++w) {
    workers.emplace_back([&, w] {
      for (;;) {
        std::string payload;
        {
          std::unique_lock<std::mutex> lock(replicas[static_cast<std::size_t>(w)].mu);
          replicas[static_cast<std::size_t>(w)].cv.wait(lock, [&] {
            return !replicas[static_cast<std::size_t>(w)].q.empty() ||
                   replicas[static_cast<std::size_t>(w)].closed.load(std::memory_order_acquire);
          });
          if (replicas[static_cast<std::size_t>(w)].q.empty() &&
              replicas[static_cast<std::size_t>(w)].closed.load(std::memory_order_acquire)) {
            break;
          }
          if (replicas[static_cast<std::size_t>(w)].q.empty()) {
            continue;
          }
          payload = std::move(replicas[static_cast<std::size_t>(w)].q.front());
          replicas[static_cast<std::size_t>(w)].q.pop();
        }
        replicas[static_cast<std::size_t>(w)].cv.notify_one();
        ++received_per[static_cast<std::size_t>(w)];
        std::cout << "  [replica " << w << "] applied snapshot: " << payload << "\n";
      }
    });
  }

  // Dispatcher thread: single owner of the consumer channel
  // This is the ONLY thread that reads from the SPSC channel
  std::thread dispatcher_thread([&] {
    for (int round = 0; round < kConfigRounds; ++round) {
      std::string config_snapshot;
      bool received = false;

      while (!received) {
        const bool has_data = consumer.poll([&](void* data, std::uint32_t length) {
          if (length != kSlotBytes) {
            return;
          }

          const char* payload = static_cast<const char*>(data);
          const std::size_t payload_length = ::strnlen(payload, static_cast<std::size_t>(length));
          config_snapshot.assign(payload, payload_length);
          received = true;
        });

        if (!has_data) {
          // Efficient atomic wait (no busy polling)
          const std::uint32_t current_seq = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
          xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, current_seq);
        }
      }

      std::cout << "  [dispatcher] received snapshot, broadcasting to " << kReplicaWorkers << " replicas\n";

      // Distribute copy to each replica queue
      for (int w = 0; w < kReplicaWorkers; ++w) {
        {
          std::lock_guard<std::mutex> lock(replicas[static_cast<std::size_t>(w)].mu);
          replicas[static_cast<std::size_t>(w)].q.push(config_snapshot);
        }
        replicas[static_cast<std::size_t>(w)].cv.notify_one();
      }
    }

    // Signal all workers that no more configurations will arrive
    for (auto& replica : replicas) {
      replica.closed.store(true, std::memory_order_release);
      replica.cv.notify_all();
    }
  });

  std::cout << "\n✓ Publishing " << kConfigRounds << " configuration snapshots\n\n";

  for (int round = 0; round < kConfigRounds; ++round) {
    std::string snapshot = build_config_snapshot(round + 1);

    // Truncate if needed to fit fixed slot size
    if (snapshot.size() + 1 > kSlotBytes) {
      snapshot.resize(static_cast<std::size_t>(kSlotBytes - 1), '.');
    }

    producer.send_fixed_bytes(snapshot.data(), static_cast<std::uint32_t>(snapshot.size()));
  }

  // Wait for dispatcher and all workers to complete
  dispatcher_thread.join();
  for (auto& worker : workers) {
    worker.join();
  }

  // Verify all replicas received all configuration updates
  bool all_ok = true;
  std::cout << "\n--- Verification results ---\n";

  for (int w = 0; w < kReplicaWorkers; ++w) {
    const int received = received_per[static_cast<std::size_t>(w)];
    if (received != kConfigRounds) {
      std::cerr << "  ✗ Replica " << w << ": expected " << kConfigRounds << ", received " << received << "\n";
      all_ok = false;
    } else {
      std::cout << "  ✓ Replica " << w << ": received all " << kConfigRounds << " snapshots\n";
    }
  }

  if (!all_ok) {
    xproc::shm::shm::unlink(shm_path);
    return 1;
  }

  std::cout << "\n✓ spmc_config_broadcast_demo completed successfully\n";
  std::cout << "  Demonstrated: SPSC channel + user space fan-out pattern\n";

  xproc::shm::shm::unlink(shm_path);
  return 0;
}
