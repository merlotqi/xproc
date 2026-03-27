#pragma once

#include <atomic>
#include <cstdint>
#include <utility>
#include <xproc/ringbuffer/details/varlen_header.hpp>
#include <xproc/ringbuffer/ringbuffer_view.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc {
namespace ringbuffer {

class varlen_reader : public ringbuffer_view {
 public:
  using ringbuffer_view::ringbuffer_view;

  template <typename F>
  bool try_read(F&& handler) {
    uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    uint64_t write_end = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    if (write_end == curr_read) {
      return false;
    }

    auto* h = reinterpret_cast<details::varlen_message_header*>(get_ptr(curr_read));
    uint32_t status = h->status.load(std::memory_order_acquire);

    if (status == 1) {
      handler(get_ptr(curr_read + sizeof(details::varlen_message_header)), h->length);

      uint32_t total_len = align_size(h->length + sizeof(details::varlen_message_header));
      header_->rb_meta.read_pos.store(curr_read + total_len, std::memory_order_release);
      header_->rb_meta.read_wake_seq.fetch_add(1, std::memory_order_release);
      sync::atomic_notify_one(&header_->rb_meta.read_wake_seq);
      return true;
    }
    if (status == 2) {
      uint64_t to_end = bytes_to_end(curr_read);
      header_->rb_meta.read_pos.store(curr_read + to_end, std::memory_order_release);
      header_->rb_meta.read_wake_seq.fetch_add(1, std::memory_order_release);
      sync::atomic_notify_one(&header_->rb_meta.read_wake_seq);
      return try_read(std::forward<F>(handler));
    }

    return false;
  }

  // Observer: walk from current read_pos without mutating shared state (skips dummy slots locally).
  template <typename F>
  bool try_peek(F&& handler) const {
    uint64_t r = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    const uint64_t write_end = header_->rb_meta.write_pos.load(std::memory_order_acquire);

    while (r != write_end) {
      const auto* h = reinterpret_cast<const details::varlen_message_header*>(get_ptr(r));
      const uint32_t status = h->status.load(std::memory_order_acquire);

      if (status == 1) {
        std::forward<F>(handler)(static_cast<const void*>(get_ptr(r + sizeof(details::varlen_message_header))),
                                 h->length);
        return true;
      }
      if (status == 2) {
        r += bytes_to_end(r);
        continue;
      }
      return false;
    }
    return false;
  }
};

}  // namespace ringbuffer
}  // namespace xproc
