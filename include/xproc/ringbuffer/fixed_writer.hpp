#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <xproc/ringbuffer/detail/fixed_header.hpp>
#include <xproc/ringbuffer/reserve_result.hpp>
#include <xproc/ringbuffer/ringbuffer_view.hpp>
#include <xproc/sync/atomic_backoff.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc::ringbuffer {

class fixed_writer : public ringbuffer_view {
 public:
  using ringbuffer_view::ringbuffer_view;

  void* reserve(uint32_t item_size, uint64_t& out_pos) {
    const uint32_t total_len = aligned_total_len(item_size);
    if (total_len > header_->data_capacity) {
      throw std::length_error("fixed_writer::reserve: message is larger than ring capacity");
    }
    while (true) {
      reserve_result rr = try_reserve(item_size);
      if (rr) {
        out_pos = rr.position;
        return rr.payload;
      }
      const uint32_t wake = header_->rb_meta.read_wake_seq.load(std::memory_order_relaxed);
      backoff_.pause(header_->rb_meta.read_wake_seq, wake);
    }
  }

  reserve_result try_reserve(uint32_t item_size) {
    const uint32_t total_len = aligned_total_len(item_size);
    if (total_len > header_->data_capacity) {
      return {reserve_status::message_too_large, nullptr, 0};
    }

    uint64_t curr_write = header_->rb_meta.write_pos.load(std::memory_order_relaxed);
    uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    if (curr_write + total_len - curr_read > header_->data_capacity) {
      return {reserve_status::full, nullptr, 0};
    }

    if (!header_->rb_meta.write_pos.compare_exchange_strong(curr_write, curr_write + total_len)) {
      return {reserve_status::full, nullptr, 0};
    }

    auto* h = reinterpret_cast<detail::fixed_message_header*>(get_ptr(curr_write));
    h->status.store(0, std::memory_order_relaxed);
    return {reserve_status::ok, get_ptr(curr_write + sizeof(detail::fixed_message_header)), curr_write};
  }

  template <typename Rep, typename Period>
  reserve_result reserve_for(uint32_t item_size, const std::chrono::duration<Rep, Period>& timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      reserve_result rr = try_reserve(item_size);
      if (rr.status != reserve_status::full) {
        return rr;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return {reserve_status::timeout, nullptr, 0};
      }
      std::this_thread::yield();
    }
  }

  void commit(uint64_t pos) {
    auto* h = reinterpret_cast<detail::fixed_message_header*>(get_ptr(pos));
    h->status.store(1, std::memory_order_release);
    header_->rb_meta.commit_seq.fetch_add(1, std::memory_order_release);
    sync::atomic_notify_one(&header_->rb_meta.commit_seq);
  }

 private:
  uint32_t aligned_total_len(uint32_t item_size) const noexcept {
    return align_size(item_size + sizeof(detail::fixed_message_header));
  }

  sync::atomic_backoff backoff_;
};

}  // namespace xproc::ringbuffer
