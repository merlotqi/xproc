// Deterministic full-ring test (Linux): pipe byte synchronizes producer entering third reserve.

#if !defined(__linux__)

int main() { return 0; }

#else

#include <unistd.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <xproc/xproc.hpp>

namespace {

void init_header(xproc::shm::shm_control_block &h, std::uint64_t cap, std::uint32_t layout_type,
                 std::uint32_t data_align) {
  using lm = xproc::shm::shm_layout_manager;
  h.magic = lm::EXPECTED_MAGIC;
  h.version_major = lm::VERSION_MAJOR;
  h.version_minor = lm::VERSION_MINOR;
  h.header_size = sizeof(xproc::shm::shm_control_block);
  h.layout_type = layout_type;
  h.rb_meta.write_pos.store(0, std::memory_order_relaxed);
  h.rb_meta.read_pos.store(0, std::memory_order_relaxed);
  h.rb_meta.commit_seq.store(0, std::memory_order_relaxed);
  h.rb_meta.read_wake_seq.store(0, std::memory_order_relaxed);
  h.data_capacity = cap;
  h.data_alignment = data_align ? data_align : 8u;
  h.attach_count.store(0, std::memory_order_relaxed);
  h.is_ready.store(true, std::memory_order_release);
  h.producer_pid.store(0, std::memory_order_relaxed);
}

template <std::size_t N>
struct alignas(xproc::shm::shm_control_block) ring_arena {
  std::array<std::uint8_t, N> bytes{};
};

}  // namespace

int main() {
  constexpr std::uint32_t item = 8;
  constexpr std::uint64_t cap = 32;
  constexpr std::size_t total = sizeof(xproc::shm::shm_control_block) + static_cast<std::size_t>(cap);
  ring_arena<total> arena{};
  auto *hdr = reinterpret_cast<xproc::shm::shm_control_block *>(arena.bytes.data());
  new (hdr) xproc::shm::shm_control_block{};
  init_header(*hdr, cap, 0, 8);

  xproc::ringbuffer::fixed_writer w(hdr);
  xproc::ringbuffer::fixed_reader r(hdr);

  std::uint64_t pos0 = 0;
  void *buf0 = w.reserve(item, pos0);
  std::memcpy(buf0, "aaaaaaaa", item);
  w.commit(pos0);

  std::uint64_t pos1 = 0;
  void *buf1 = w.reserve(item, pos1);
  std::memcpy(buf1, "bbbbbbbb", item);
  w.commit(pos1);

  int pipefd[2];
  assert(pipe(pipefd) == 0);

  std::atomic<bool> third_done{false};
  std::thread producer([&] {
    char sync = 1;
    assert(write(pipefd[1], &sync, 1) == 1);
    std::uint64_t pos2 = 0;
    void *buf2 = w.reserve(item, pos2);
    std::memcpy(buf2, "cccccccc", item);
    w.commit(pos2);
    third_done.store(true, std::memory_order_release);
  });

  char sink;
  assert(read(pipefd[0], &sink, 1) == 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  assert(!third_done.load(std::memory_order_acquire));

  assert(r.try_read(item, [](void *) {}));

  producer.join();
  assert(third_done.load());

  close(pipefd[0]);
  close(pipefd[1]);

  assert(r.try_read(item, [](void *p) { assert(std::memcmp(p, "bbbbbbbb", item) == 0); }));
  assert(r.try_read(item, [](void *p) { assert(std::memcmp(p, "cccccccc", item) == 0); }));
  return 0;
}

#endif
