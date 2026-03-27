#pragma once

#include <cstddef>
#include <cstdint>
#include <xproc/ringbuffer/ringbuffer_error.hpp>
#include <xproc/shm/shm_layout.hpp>

namespace xproc {
namespace ringbuffer {

// ringbuffer_error: status enum for wrappers/tests (see ringbuffer_error.hpp); not returned from fixed_* / varlen_* hot
// paths. Optional polymorphic facade over a mapped control block (tests, observers). SPSC implementations remain
// non-virtual on the hot path.
class IRingBuffer {
 public:
  virtual ~IRingBuffer() = default;
  virtual std::size_t capacity_bytes() const noexcept = 0;
  virtual std::uint32_t data_alignment() const noexcept = 0;
};

class control_block_ring_facade final : public IRingBuffer {
 public:
  explicit control_block_ring_facade(const shm::shm_control_block* header) noexcept : header_(header) {}

  std::size_t capacity_bytes() const noexcept override {
    return header_ ? static_cast<std::size_t>(header_->data_capacity) : 0;
  }

  std::uint32_t data_alignment() const noexcept override { return header_ ? header_->data_alignment : 0; }

  const shm::shm_control_block* control_block() const noexcept { return header_; }

 private:
  const shm::shm_control_block* header_;
};

}  // namespace ringbuffer
}  // namespace xproc
