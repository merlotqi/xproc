// ipc_runtime + TaskFlow: per-message three-stage pipeline (separate submit_task + sync), multi-message batch.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <taskflow/task_manager.hpp>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

struct PipelineState {
  std::uint32_t raw{0};
  std::uint64_t decoded{0};
  std::uint64_t computed{0};
};

void submit_and_wait(taskflow::TaskManager& manager, std::function<void(taskflow::TaskCtx&)> body) {
  auto settled = std::make_shared<std::promise<void>>();
  std::future<void> fut = settled->get_future();
  manager.submit_task([body = std::move(body), settled](taskflow::TaskCtx& ctx) mutable {
    body(ctx);
    if (!ctx.is_completed()) {
      ctx.success();
    }
    settled->set_value();
  });
  fut.wait();
}

// Cheap "work" so stages are visible under a profiler; keeps demo deterministic.
std::uint64_t scramble(std::uint64_t x) {
  for (int i = 0; i < 50'000; ++i) {
    x = x * 6364136223846793005ULL + 1;
  }
  return x;
}

}  // namespace

int main() {
  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(std::max<std::size_t>(2, std::thread::hardware_concurrency()));

  const std::string path = "/xproc_ipc_taskflow_pipe_" + std::to_string(xproc::platform::current_process_id());
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(65536);
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  xproc::ipc::runtime runtime(consumer);

  constexpr int k_expected_msgs = 5;
  std::mutex results_mu;
  std::vector<std::uint64_t> finalized;
  std::atomic<int> received{0};
  std::atomic<bool> done{false};

  std::thread rt([&] {
    auto pool_executor = [&manager](auto task) {
      auto settled = std::make_shared<std::promise<void>>();
      std::future<void> fut = settled->get_future();
      manager.submit_task([t = std::move(task), settled](taskflow::TaskCtx& ctx) mutable {
        t();
        ctx.success();
        settled->set_value();
      });
      fut.wait();
    };

    runtime.run(pool_executor, [&](const std::uint8_t* data, std::size_t len) {
      if (len != sizeof(std::uint32_t)) {
        std::cerr << "unexpected len: " << len << "\n";
        runtime.stop();
        done.store(true, std::memory_order_release);
        return;
      }
      std::uint32_t v = 0;
      std::memcpy(&v, data, sizeof(v));

      auto state = std::make_shared<PipelineState>();
      state->raw = v;

      submit_and_wait(manager, [state](taskflow::TaskCtx& ctx) {
        ctx.update_progress(0.15f, "decode");
        state->decoded = static_cast<std::uint64_t>(state->raw) * 1001u + 7u;
      });

      submit_and_wait(manager, [state](taskflow::TaskCtx& ctx) {
        ctx.update_progress(0.55f, "compute");
        state->computed = scramble(state->decoded);
      });

      submit_and_wait(manager, [&](taskflow::TaskCtx& ctx) {
        ctx.update_progress(0.95f, "finalize");
        std::lock_guard<std::mutex> lock(results_mu);
        finalized.push_back(state->computed);
      });

      const int n = received.fetch_add(1, std::memory_order_acq_rel) + 1;
      std::cout << "pipeline done for msg " << n << " raw=" << v << " final=" << state->computed << "\n";

      if (n >= k_expected_msgs) {
        runtime.stop();
        done.store(true, std::memory_order_release);
      }
    });
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  for (int i = 1; i <= k_expected_msgs; ++i) {
    producer.send_fixed<std::uint32_t>(static_cast<std::uint32_t>(i * 17u));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  for (int i = 0; i < 400 && !done.load(std::memory_order_acquire); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  runtime.stop();
  rt.join();
  manager.stop_processing();

  xproc::core::shm::unlink(path);

  if (!done.load(std::memory_order_acquire)) {
    std::cerr << "timeout waiting for consumer\n";
    return 2;
  }
  if (static_cast<int>(finalized.size()) != k_expected_msgs) {
    std::cerr << "expected " << k_expected_msgs << " results, got " << finalized.size() << "\n";
    return 3;
  }

  std::uint64_t sum = 0;
  for (std::uint64_t x : finalized) {
    sum ^= x;
  }
  std::cout << "batch xor checksum: " << sum << " (vector size " << finalized.size() << ")\n";
  return 0;
}
