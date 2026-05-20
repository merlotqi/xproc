#pragma once

#include <atomic>
#include <xproc/platform/platform.hpp>

namespace xproc::core {

// SPSC ring indices and futex wait words. write_pos/read_pos are monotonic logical
// offsets; the data region index is pos % data_capacity.
struct meta {
  XPROC_ALIGNAS_CACHE_LINE std::atomic<uint64_t> write_pos{0};
  uint8_t padding1[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<uint64_t>)];

  XPROC_ALIGNAS_CACHE_LINE std::atomic<uint64_t> read_pos{0};
  uint8_t padding2[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<uint64_t>)];

  // Bumped by producer after each message becomes readable (32-bit for Linux futex).
  XPROC_ALIGNAS_CACHE_LINE std::atomic<uint32_t> commit_seq{0};
  uint8_t padding3[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<uint32_t>)];

  // Bumped by consumer whenever read_pos advances (including dummy padding); wakes producers blocked on space.
  XPROC_ALIGNAS_CACHE_LINE std::atomic<uint32_t> read_wake_seq{0};
  uint8_t padding4[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<uint32_t>)];
};

struct XPROC_ALIGNAS_CACHE_LINE control_block {
  uint32_t magic;
  uint16_t version_major;
  uint32_t version_minor;
  uint32_t header_size;
  uint32_t layout_type;  // 0: fixed, 1: variable length

  std::atomic<uint32_t> attach_count{0};
  std::atomic<bool> is_ready{false};
  std::atomic<int32_t> producer_pid{0};

  uint8_t padding_identity[XPROC_CACHE_LINE_SIZE - 28];

  meta rb_meta;

  uint64_t data_capacity;
  uint32_t data_alignment;
  uint32_t fixed_item_size{0};       // Fixed channel manifest field; 0 for varlen channels.
  uint64_t schema_id{0};             // Optional user-supplied protocol/schema identifier.
  uint64_t creator_timestamp_ns{0};  // Optional creator-supplied persisted metadata.
  uint64_t creator_flags{0};         // Optional creator-supplied persisted metadata.

  uint64_t reserved[1];
};

}  // namespace xproc::core
