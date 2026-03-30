#include <xproc/ipc/channel.hpp>
#include <xproc/ipc/channel_interface.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc {
namespace ipc {

shm_producer::shm_producer(const transport_options& opts) : ch_(opts) {}

void shm_producer::send_fixed_bytes(const void* data, std::uint32_t payload_len) {
  ch_.send_fixed_bytes(data, payload_len);
}

void shm_producer::send_fixed_sized(const void* data, std::uint32_t byte_length) {
  ch_.send_fixed_sized(data, byte_length);
}

void shm_producer::send_varlen(const void* data, std::uint32_t len) { ch_.send_varlen(data, len); }

shm_consumer::shm_consumer(const transport_options& opts) : ch_(opts) {}

bool shm_consumer::poll_impl(const std::function<void(void*, std::uint32_t)>& handler) { return ch_.poll(handler); }

void shm_consumer::wait() {
  shm::control_block* h = ch_.header();
  if (!h) {
    return;
  }
  const std::uint32_t last = h->rb_meta.commit_seq.load(std::memory_order_acquire);
  sync::atomic_wait(&h->rb_meta.commit_seq, last);
}

}  // namespace ipc
}  // namespace xproc
