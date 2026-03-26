#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include <xproc/xproc.hpp>

namespace {

void test_observer_peek_then_consumer_drains() {
  const std::string path = "/xproc_ipc_observer_attach_test";
  xproc::shm::shm::unlink(path);

  constexpr std::size_t total = sizeof(xproc::shm::shm_control_block) + 512;
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = total;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);

  {
    xproc::ipc::producer_channel prod(opts);
    xproc::ipc::ipc_observer obs(opts);
    xproc::ipc::consumer_channel cons(opts);

    assert(obs.attach_count() >= 2u);

    prod.send_fixed<std::uint32_t>(0x99aabbccu);

    bool peeked = false;
    while (!peeked) {
      peeked = obs.peek([&](const void *p, std::uint32_t len) {
        assert(len == sizeof(std::uint32_t));
        std::uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        assert(v == 0x99aabbccu);
      });
      if (!peeked) {
        const std::uint32_t c = obs.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&obs.header()->rb_meta.commit_seq, c);
      }
    }

    bool consumed = false;
    while (!consumed) {
      consumed = cons.poll([&](void *p, std::uint32_t len) {
        assert(len == sizeof(std::uint32_t));
        std::uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        assert(v == 0x99aabbccu);
      });
      if (!consumed) {
        const std::uint32_t c = cons.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&cons.header()->rb_meta.commit_seq, c);
      }
    }

    const xproc::ipc::ipc_ring_snapshot snap = obs.ring_snapshot();
    assert(snap.write_pos >= snap.read_pos);
  }

  xproc::shm::shm::unlink(path);
}

}  // namespace

int main() {
  test_observer_peek_then_consumer_drains();
  return 0;
}
