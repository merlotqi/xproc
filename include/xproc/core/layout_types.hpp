#pragma once

#include <atomic>
#include <spscring/control_block.hpp>
#include <xproc/platform/platform.hpp>

namespace xproc::core {

// xproc control_block wraps spscring's ABI-compatible control block (first
// member) and appends xproc-specific IPC metadata (attach_count, is_ready,
// producer_pid, schema_id, creator_timestamp_ns, creator_flags).
//
// Because spscring::control_block is the first member, a pointer to this
// struct can be reinterpret_cast to spscring::control_block* for ring_view
// construction.  header_size is set to sizeof(control_block) so the ring
// data region begins right after the xproc tail.
struct XPROC_ALIGNAS_CACHE_LINE control_block {
  spscring::control_block spscring_cb;

  // ---- xproc-specific IPC metadata (occupies spscring reserved tail) ----
  std::atomic<std::uint32_t> attach_count{0};
  std::atomic<bool> is_ready{false};
  std::atomic<std::int32_t> producer_pid{0};

  std::uint8_t padding_identity[XPROC_CACHE_LINE_SIZE - 28];

  std::uint64_t schema_id{0};
  std::uint64_t creator_timestamp_ns{0};
  std::uint64_t creator_flags{0};
  std::uint64_t reserved_xproc[1]{0};
};

}  // namespace xproc::core
