// MPMC in-process queue (mutex + deque + condition variables), then a small SPSC
// "bridge" over shared memory: one thread drains the queue into ipc::producer;
// another thread ipc::consumer::poll and hands off to multiple drain workers.
// The ring itself stays single-producer single-reader throughout.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {
constexpr int kMpmcPushers = 4;
constexpr int kMpmcPoppers = 4;
constexpr int kItemsPerPusher = 125;
constexpr int kMpmcTotal = kMpmcPushers * kItemsPerPusher;

constexpr int kBridgeMessages = 40;
constexpr int kBridgeDrainers = 2;
}  // namespace

int main() {
  const std::string path =
      "/xproc_mpmc_bridge_" + std::to_string(static_cast<long long>(xproc::current_process_id()));

  xproc::shm::shm::unlink(path);

  // --- Part 1: MPMC on a shared deque ---
  std::mutex dq_mu;
  std::condition_variable dq_cv_push;
  std::condition_variable dq_cv_pop;
  std::deque<std::uint32_t> dq;
  std::atomic<bool> all_pushes_done{false};

  std::vector<std::thread> pushers;
  for (int t = 0; t < kMpmcPushers; ++t) {
    pushers.emplace_back([&, t] {
      const int base = t * kItemsPerPusher;
      for (int i = 0; i < kItemsPerPusher; ++i) {
        const std::uint32_t v = static_cast<std::uint32_t>(base + i + 1u);
        {
          std::unique_lock<std::mutex> lock(dq_mu);
          dq_cv_push.wait(lock, [&] { return static_cast<int>(dq.size()) < 64; });
          dq.push_back(v);
        }
        dq_cv_pop.notify_one();
      }
    });
  }

  std::atomic<int> mpmc_popped{0};
  std::uint64_t mpmc_sum = 0;
  std::mutex sum_mu;

  std::vector<std::thread> poppers;
  for (int p = 0; p < kMpmcPoppers; ++p) {
    (void)p;
    poppers.emplace_back([&] {
      for (;;) {
        std::uint32_t v = 0;
        {
          std::unique_lock<std::mutex> lock(dq_mu);
          dq_cv_pop.wait(lock, [&] { return !dq.empty() || all_pushes_done.load(std::memory_order_acquire); });
          if (dq.empty() && all_pushes_done.load(std::memory_order_acquire)) {
            break;
          }
          if (dq.empty()) {
            continue;
          }
          v = dq.front();
          dq.pop_front();
        }
        dq_cv_push.notify_one();
        {
          std::lock_guard<std::mutex> lock(sum_mu);
          mpmc_sum += static_cast<std::uint64_t>(v);
        }
        mpmc_popped.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto& th : pushers) {
    th.join();
  }
  all_pushes_done.store(true, std::memory_order_release);
  dq_cv_pop.notify_all();

  for (auto& th : poppers) {
    th.join();
  }

  if (mpmc_popped.load(std::memory_order_relaxed) != kMpmcTotal) {
    std::cerr << "mpmc_inprocess_bridge_demo: MPMC phase expected " << kMpmcTotal << " pops\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }
  {
    std::uint64_t expected = 0;
    for (int i = 1; i <= kMpmcTotal; ++i) {
      expected += static_cast<std::uint64_t>(i);
    }
    if (mpmc_sum != expected) {
      std::cerr << "mpmc_inprocess_bridge_demo: MPMC checksum mismatch\n";
      xproc::shm::shm::unlink(path);
      return 1;
    }
  }

  // --- Part 2: SPSC bridge + fan-out drain ---
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 128 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  std::mutex out_mu;
  std::condition_variable out_cv;
  std::queue<std::uint32_t> out_q;
  std::atomic<bool> bridge_rx_done{false};

  std::atomic<std::uint64_t> bridge_sum{0};
  std::atomic<int> bridge_drained{0};

  std::vector<std::thread> drainers;
  for (int d = 0; d < kBridgeDrainers; ++d) {
    (void)d;
    drainers.emplace_back([&] {
      for (;;) {
        std::uint32_t v = 0;
        {
          std::unique_lock<std::mutex> lock(out_mu);
          out_cv.wait(lock, [&] { return !out_q.empty() || bridge_rx_done.load(std::memory_order_acquire); });
          if (out_q.empty() && bridge_rx_done.load(std::memory_order_acquire)) {
            break;
          }
          if (out_q.empty()) {
            continue;
          }
          v = out_q.front();
          out_q.pop();
        }
        out_cv.notify_one();
        bridge_sum.fetch_add(static_cast<std::uint64_t>(v), std::memory_order_relaxed);
        bridge_drained.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::thread bridge_rx([&] {
    int n = 0;
    while (n < kBridgeMessages) {
      const bool got = consumer.poll([&](void* p, std::uint32_t len) {
        if (len != sizeof(std::uint32_t)) {
          return;
        }
        const std::uint32_t v = *static_cast<std::uint32_t*>(p);
        {
          std::lock_guard<std::mutex> lock(out_mu);
          out_q.push(v);
        }
        out_cv.notify_one();
        ++n;
      });
      if (!got) {
        const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
    bridge_rx_done.store(true, std::memory_order_release);
    out_cv.notify_all();
  });

  std::thread bridge_tx([&] {
    for (int i = 1; i <= kBridgeMessages; ++i) {
      producer.send_fixed(static_cast<std::uint32_t>(i));
    }
  });

  bridge_tx.join();
  bridge_rx.join();
  for (auto& th : drainers) {
    th.join();
  }

  if (bridge_drained.load(std::memory_order_relaxed) != kBridgeMessages) {
    std::cerr << "mpmc_inprocess_bridge_demo: bridge drain count mismatch\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }
  const std::uint64_t bridge_expected =
      static_cast<std::uint64_t>(kBridgeMessages) * static_cast<std::uint64_t>(kBridgeMessages + 1) / 2u;
  if (bridge_sum.load(std::memory_order_relaxed) != bridge_expected) {
    std::cerr << "mpmc_inprocess_bridge_demo: bridge checksum mismatch\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }

  std::cout << "mpmc_inprocess_bridge_demo: ok (MPMC deque " << kMpmcTotal << " items, then SPSC bridge "
            << kBridgeMessages << " msgs)\n";
  xproc::shm::shm::unlink(path);
  return 0;
}
