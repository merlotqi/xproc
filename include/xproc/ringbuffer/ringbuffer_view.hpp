#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <xproc/core/shm_layout.hpp>

namespace xproc::ringbuffer {

class ringbuffer_view {
 public:
  explicit ringbuffer_view(core::control_block* header)
      : header_(header), data_(reinterpret_cast<uint8_t*>(header) + header->header_size) {}

  inline std::size_t capacity() const { return header_->data_capacity; }

  inline std::size_t used_bytes() const {
    const auto write = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    const auto used = write >= read ? (write - read) : 0;
    const auto cap = static_cast<std::uint64_t>(header_->data_capacity);
    return static_cast<std::size_t>(used > cap ? cap : used);
  }

  inline std::size_t available_bytes() const {
    return capacity() - used_bytes();
  }

  inline double fill_ratio() const {
    const auto cap = capacity();
    if (cap == 0) {
      return 0.0;
    }
    return static_cast<double>(used_bytes()) / static_cast<double>(cap);
  }

 protected:
  inline size_t map_pos(uint64_t pos) const { return static_cast<size_t>(pos % header_->data_capacity); }

  inline uint8_t* get_ptr(uint64_t pos) { return data_ + map_pos(pos); }

  inline const uint8_t* get_ptr(uint64_t pos) const { return data_ + map_pos(pos); }

  inline uint64_t bytes_to_end(uint64_t pos) const { return header_->data_capacity - map_pos(pos); }

  inline uint32_t align_size(uint32_t size) const noexcept {
    uint32_t align = header_->data_alignment;
    return (size + align - 1) & ~(align - 1);
  }

 protected:
  core::control_block* header_;
  uint8_t* data_;
};

}  // namespace xproc::ringbuffer
