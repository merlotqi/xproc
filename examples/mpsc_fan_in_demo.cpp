// MPSC pattern on top of xproc's SPSC channel: many threads enqueue work; one thread
// is the sole ipc::producer (fan-in). xproc remains single-writer single-reader.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

namespace {
constexpr int kProducerThreads = 4;
constexpr int kItemsPerProducer = 250;
constexpr int kTotalItems = kProducerThreads * kItemsPerProducer;
}  // namespace

int main() {
  const std::string path =
      "/xproc_mpsc_fan_in_" + std::to_string(static_cast<long long>(xproc::current_process_id()));

  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 256 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  std::mutex q_mu;
  std::condition_variable q_cv;
  std::queue<std::uint32_t> pending;
  std::atomic<bool> producers_done{false};

  std::thread fan_in([&] {
    for (;;) {
      std::uint32_t v = 0;
      {
        std::unique_lock<std::mutex> lock(q_mu);
        q_cv.wait(lock, [&] { return !pending.empty() || producers_done.load(std::memory_order_acquire); });
        if (pending.empty() && producers_done.load(std::memory_order_acquire)) {
          break;
        }
        if (pending.empty()) {
          continue;
        }
        v = pending.front();
        pending.pop();
      }
      q_cv.notify_one();
      producer.send_fixed(v);
    }
  });

  std::vector<std::thread> producers;
  producers.reserve(kProducerThreads);
  for (int t = 0; t < kProducerThreads; ++t) {
    producers.emplace_back([&, t] {
      const int base = t * kItemsPerProducer;
      for (int i = 0; i < kItemsPerProducer; ++i) {
        const std::uint32_t value = static_cast<std::uint32_t>(base + i + 1u);
        {
          std::lock_guard<std::mutex> lock(q_mu);
          pending.push(value);
        }
        q_cv.notify_one();
      }
    });
  }

  for (auto& th : producers) {
    th.join();
  }
  producers_done.store(true, std::memory_order_release);
  q_cv.notify_one();
  fan_in.join();

  std::atomic<int> received{0};
  std::uint64_t sum = 0;
  std::mutex sum_mu;

  std::thread drain([&] {
    int n = 0;
    while (n < kTotalItems) {
      const bool got = consumer.poll([&](void* p, std::uint32_t len) {
        if (len != sizeof(std::uint32_t)) {
          return;
        }
        const std::uint32_t v = *static_cast<std::uint32_t*>(p);
        {
          std::lock_guard<std::mutex> lock(sum_mu);
          sum += static_cast<std::uint64_t>(v);
        }
        received.fetch_add(1, std::memory_order_relaxed);
        ++n;
      });
      if (!got) {
        const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  });

  drain.join();

  const std::uint64_t expected_sum = [] {
    std::uint64_t s = 0;
    for (int i = 1; i <= kTotalItems; ++i) {
      s += static_cast<std::uint64_t>(i);
    }
    return s;
  }();

  if (received.load(std::memory_order_relaxed) != kTotalItems) {
    std::cerr << "mpsc_fan_in_demo: expected " << kTotalItems << " messages, got "
              << received.load(std::memory_order_relaxed) << "\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }
  if (sum != expected_sum) {
    std::cerr << "mpsc_fan_in_demo: checksum mismatch\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }

  std::cout << "mpsc_fan_in_demo: ok (" << kTotalItems << " items, fan-in -> sole producer)\n";
  xproc::shm::shm::unlink(path);
  return 0;
}
