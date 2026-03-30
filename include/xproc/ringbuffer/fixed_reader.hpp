#pragma once

#include <atomic>
#include <cstdint>
#include <xproc/ringbuffer/details/fixed_header.hpp>
#include <xproc/ringbuffer/ringbuffer_view.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc {
namespace ringbuffer {

class fixed_reader : public ringbuffer_view {
 public:
  using ringbuffer_view::ringbuffer_view;

  template <typename F>
  bool read(uint32_t item_size, F&& handler) {
    uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    uint64_t write_end = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    if (write_end == curr_read) {
      return false;
    }

    uint32_t total_len = align_size(item_size + sizeof(details::fixed_message_header));

    auto* h = reinterpret_cast<details::fixed_message_header*>(get_ptr(curr_read));
    if (h->status.load(std::memory_order_acquire) == 1) {
      handler(get_ptr(curr_read + sizeof(details::fixed_message_header)));
      h->status.store(0, std::memory_order_relaxed);
      header_->rb_meta.read_pos.store(curr_read + total_len, std::memory_order_release);
      header_->rb_meta.read_wake_seq.fetch_add(1, std::memory_order_release);
      sync::atomic_notify_one(&header_->rb_meta.read_wake_seq);
      return true;
    }
    return false;
  }

  // Observer / inspector: deliver committed payload without advancing read_pos or wake_seq.
  template <typename F>
  bool peek(uint32_t item_size, F&& handler) const {
    uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    uint64_t write_end = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    if (write_end == curr_read) {
      return false;
    }

    uint32_t total_len = align_size(item_size + sizeof(details::fixed_message_header));

    const auto* h = reinterpret_cast<const details::fixed_message_header*>(get_ptr(curr_read));
    if (h->status.load(std::memory_order_acquire) == 1) {
      std::forward<F>(handler)(static_cast<const void*>(get_ptr(curr_read + sizeof(details::fixed_message_header))),
                               item_size);
      (void)total_len;
      return true;
    }
    return false;
  }
};

}  // namespace ringbuffer
}  // namespace xproc
