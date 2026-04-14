// SPMC pattern on top of xproc's SPSC channel: one thread owns ipc::consumer::poll;
// each message is copied and handed to worker threads (fan-out). Only one reader
// advances the ring.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {
constexpr int kWorkerThreads = 4;
constexpr int kMessages = 200;
}  // namespace

int main() {
  const std::string path =
      "/xproc_spmc_fan_out_" + std::to_string(static_cast<long long>(xproc::current_process_id()));

  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 256 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  std::mutex work_mu;
  std::condition_variable work_cv;
  std::queue<std::uint32_t> work;
  std::atomic<bool> feed_done{false};

  std::atomic<std::uint64_t> processed_sum{0};
  std::atomic<int> processed_count{0};

  std::vector<std::thread> workers;
  workers.reserve(kWorkerThreads);
  for (int w = 0; w < kWorkerThreads; ++w) {
    workers.emplace_back([&] {
      for (;;) {
        std::uint32_t v = 0;
        {
          std::unique_lock<std::mutex> lock(work_mu);
          work_cv.wait(lock, [&] { return !work.empty() || feed_done.load(std::memory_order_acquire); });
          if (work.empty() && feed_done.load(std::memory_order_acquire)) {
            break;
          }
          if (work.empty()) {
            continue;
          }
          v = work.front();
          work.pop();
        }
        work_cv.notify_one();
        processed_sum.fetch_add(static_cast<std::uint64_t>(v), std::memory_order_relaxed);
        processed_count.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::thread reader([&] {
    int n = 0;
    while (n < kMessages) {
      const bool got = consumer.poll([&](void* p, std::uint32_t len) {
        if (len != sizeof(std::uint32_t)) {
          return;
        }
        const std::uint32_t v = *static_cast<std::uint32_t*>(p);
        {
          std::lock_guard<std::mutex> lock(work_mu);
          work.push(v);
        }
        work_cv.notify_one();
        ++n;
      });
      if (!got) {
        const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
    feed_done.store(true, std::memory_order_release);
    work_cv.notify_all();
  });

  for (int i = 1; i <= kMessages; ++i) {
    producer.send_fixed(static_cast<std::uint32_t>(i));
  }

  reader.join();
  for (auto& w : workers) {
    w.join();
  }

  const int pc = processed_count.load(std::memory_order_relaxed);
  if (pc != kMessages) {
    std::cerr << "spmc_fan_out_demo: expected " << kMessages << " processed, got " << pc << "\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }

  const std::uint64_t expected_sum = static_cast<std::uint64_t>(kMessages) * static_cast<std::uint64_t>(kMessages + 1) / 2u;
  if (processed_sum.load(std::memory_order_relaxed) != expected_sum) {
    std::cerr << "spmc_fan_out_demo: checksum mismatch\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }

  std::cout << "spmc_fan_out_demo: ok (" << kMessages << " messages, sole reader -> fan-out)\n";
  xproc::shm::shm::unlink(path);
  return 0;
}
