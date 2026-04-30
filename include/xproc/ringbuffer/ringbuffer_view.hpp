#pragma once

#include <cstddef>
#include <cstdint>
#include <xproc/core/shm_layout.hpp>

namespace xproc::ringbuffer {

class ringbuffer_view {
 public:
  explicit ringbuffer_view(core::control_block* header)
      : header_(header), data_(reinterpret_cast<uint8_t*>(header) + header->header_size) {}

  inline std::size_t capacity() const { return header_->data_capacity; }

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
