// ipc_runtime + embedded TaskFlow: each message is submitted as a disposable task on TaskManager's pool.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <taskflow/task_manager.hpp>
#include <thread>
#include <xproc/xproc.hpp>

int main() {
  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(std::max<std::size_t>(1, std::thread::hardware_concurrency()));

  const std::string path = "/xproc_ipc_taskflow_demo_" + std::to_string(xproc::platform::current_process_id());
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(16384);
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  xproc::ipc::runtime runtime(consumer);

  std::atomic<bool> done{false};
  std::thread rt([&] {
    // Wait for each submitted task to finish before returning to ipc_runtime's poll loop. On Windows,
    // atomic_wait(commit_seq) does not wake on stop(); if we returned here immediately, the run
    // thread could block in atomic_wait while the handler (on a pool thread) calls runtime.stop().
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
      std::cout << "taskflow-backed handler got: " << v << "\n";
      runtime.stop();
      done.store(true, std::memory_order_release);
    });
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  producer.send_fixed<std::uint32_t>(9001u);

  for (int i = 0; i < 200 && !done.load(std::memory_order_acquire); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  runtime.stop();
  rt.join();

  manager.stop_processing();

  xproc::shm::shm::unlink(path);
  return done.load(std::memory_order_acquire) ? 0 : 1;
}
