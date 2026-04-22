// Variable-length channel example (single process, two threads).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

int main() {
  const std::string path = "/xproc_example_varlen_inprocess";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = xproc::ipc::shm_size_for_data_capacity(32768);
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);

  std::vector<std::string> msgs = {"hello", "xproc", "variable-length", "messages"};
  std::atomic<std::size_t> recv_count{0};

  std::thread t([&] {
    while (recv_count.load(std::memory_order_acquire) < msgs.size()) {
      bool got = consumer.poll([&](void* p, std::uint32_t len) {
        std::string s(static_cast<const char*>(p), static_cast<std::size_t>(len));
        std::cout << "recv: " << s << "\n";
        const std::size_t idx = recv_count.load(std::memory_order_relaxed);
        if (idx < msgs.size() && s == msgs[idx]) {
          recv_count.fetch_add(1, std::memory_order_release);
        }
      });
      if (!got) {
        const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  });

  for (const auto& s : msgs) {
    producer.send_varlen(s.data(), static_cast<std::uint32_t>(s.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }

  t.join();
  xproc::shm::shm::unlink(path);
  return (recv_count.load() == msgs.size()) ? 0 : 1;
}
