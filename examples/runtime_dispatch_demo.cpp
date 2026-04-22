// ipc_runtime example: poll loop dispatching copied payloads.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <xproc/xproc.hpp>

int main() {
  const std::string path = "/xproc_example_runtime_dispatch";
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
    auto inline_executor = [](auto task) { task(); };
    runtime.run(inline_executor, [&](const std::uint8_t* data, std::size_t len) {
      if (len != sizeof(std::uint32_t)) {
        std::cerr << "unexpected len: " << len << "\n";
        runtime.stop();
        done.store(true, std::memory_order_release);
        return;
      }
      std::uint32_t v = 0;
      std::memcpy(&v, data, sizeof(v));
      std::cout << "runtime handler got: " << v << "\n";
      runtime.stop();
      done.store(true, std::memory_order_release);
    });
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  producer.send_fixed<std::uint32_t>(9001u);

  for (int i = 0; i < 100 && !done.load(std::memory_order_acquire); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  runtime.stop();
  rt.join();

  xproc::shm::shm::unlink(path);
  return done.load(std::memory_order_acquire) ? 0 : 1;
}
