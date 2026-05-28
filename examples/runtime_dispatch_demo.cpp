// runtime_dispatch demo: producer sends messages, runtime dispatches to worker threads.
//
// Architecture:
//   main thread                    rt thread                     worker threads
//   ──────────                    ─────────                     ──────────────
//   send_fixed(msg) ──SHM──→  poll() → copy → executor(task)
//   send_fixed(msg) ──SHM──→  poll() → copy → executor(task)
//   send_fixed(msg) ──SHM──→  poll() → copy → executor(task)
//   ... send STOP sentinel ... → handler calls stop()
//
// The executor (a simple thread pool) decouples message reception from processing.
// This is the pattern for integrating xproc into async frameworks (TaskFlow, asio, etc.).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

// Minimal thread pool so the demo is self-contained.
class simple_thread_pool {
 public:
  explicit simple_thread_pool(std::size_t threads) {
    for (std::size_t i = 0; i < threads; ++i) {
      workers_.emplace_back([this, i] {
        std::cerr << "  [worker " << i << "] started\n";
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stopped_ || !tasks_.empty(); });
            if (stopped_ && tasks_.empty()) break;
            task = std::move(tasks_.front());
            tasks_.pop();
          }
          task();
        }
      });
    }
  }

  void submit(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      tasks_.push(std::move(task));
    }
    cv_.notify_one();
  }

  ~simple_thread_pool() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stopped_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) w.join();
  }

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool stopped_{false};
};

int main() {
  const std::string path = "/xproc_example_runtime_dispatch";
  xproc::core::shm::unlink(path);

  // ---- setup: fixed channel carrying 16-byte messages ----
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(16384);
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 16;
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  xproc::ipc::runtime runtime(consumer);

  // ---- start runtime on a dedicated thread with a 2-worker pool ----
  simple_thread_pool pool(2);
  std::atomic<int> processed{0};
  constexpr int kExpected = 5;

  std::thread rt_thread([&] {
    auto executor = [&](auto task) { pool.submit(std::move(task)); };

    // Use sbo policy: each message is stack-copied into the executor lambda.
    // Under reuse_buffer or zero_copy, the data pointer is borrowed and would be
    // overwritten by subsequent polls before the pool worker reads it.
    runtime.run(
        executor,
        [&](const std::uint8_t* data, std::size_t len) {
          // Parse the 16-byte message: [id:4][value:4][padding:8]
          std::uint32_t id = 0;
          std::uint32_t val = 0;
          std::memcpy(&id, data, 4);
          std::memcpy(&val, data + 4, 4);

          // Simulate work
          std::this_thread::sleep_for(std::chrono::milliseconds(20));

          int n = processed.fetch_add(1) + 1;
          std::cerr << "  [worker] msg #" << n << " id=" << id << " value=" << val
                    << " (tid=" << std::this_thread::get_id() << ")\n";

          if (n >= kExpected) {
            runtime.stop();
          }
        },
        xproc::ipc::copy_policy::sbo);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  // ---- send a batch of messages ----
  std::cout << "=== runtime dispatch demo ===\n";
  std::cout << "sending " << kExpected << " messages...\n\n";

  for (std::uint32_t i = 0; i < kExpected; ++i) {
    struct __attribute__((packed)) {
      std::uint32_t id;
      std::uint32_t value;
      std::uint32_t pad[2]{};
    } msg;
    msg.id = i;
    msg.value = i * 100;
    producer.send_fixed(msg);
    std::cerr << "  [producer] sent id=" << i << " value=" << msg.value << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // ---- wait for completion ----
  rt_thread.join();
  xproc::core::shm::unlink(path);
  std::cout << "\nall " << processed.load() << " messages processed.\n";
  return 0;
}
