#include <atomic>
#include <cstring>
#include <xproc/platform/platform.hpp>
#include <xproc/platform/process.hpp>
#include <xproc/shm/shm_layout_manager.hpp>

#include "xproc/shm/shm_layout.hpp"

namespace xproc {
namespace shm {

control_block* layout_manager::format(shm& sm, size_t capacity, bool is_creator, uint32_t layout_type,
                                      uint32_t data_alignment, uint32_t fixed_item_size,
                                      std::uint64_t expected_schema_id, std::uint64_t creator_timestamp_ns,
                                      std::uint64_t creator_flags, attach_behavior behavior) {
  if (!sm.is_attached()) {
    return nullptr;
  }

  auto* header = static_cast<control_block*>(sm.addr());
  if (is_creator) {
    _init_header(header, capacity, layout_type, data_alignment, fixed_item_size, expected_schema_id,
                 creator_timestamp_ns, creator_flags);
  } else {
    if (!validate(header, capacity, layout_type, data_alignment, fixed_item_size, expected_schema_id)) {
      return nullptr;
    }
    if (behavior == attach_behavior::ref_count) {
      header->attach_count.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return header;
}

namespace {

bool is_power_of_two_uint32(uint32_t x) { return x >= 4u && (x & (x - 1u)) == 0u; }

}  // namespace

const char* layout_manager::validate_cstr(validate_error e) noexcept {
  switch (e) {
    case validate_error::ok:
      return "ok";
    case validate_error::not_attached:
      return "shared memory mapping is not attached";
    case validate_error::bad_magic:
      return "bad magic (not an xproc segment or corrupted)";
    case validate_error::not_ready_timeout:
      return "control block not ready (timeout waiting for is_ready)";
    case validate_error::version_mismatch:
      return "layout version mismatch";
    case validate_error::header_size_mismatch:
      return "header_size does not match this build";
    case validate_error::layout_type_mismatch:
      return "layout_type mismatch (fixed vs variable)";
    case validate_error::fixed_item_size_mismatch:
      return "fixed_item_size mismatch";
    case validate_error::schema_id_mismatch:
      return "schema_id mismatch";
    case validate_error::alignment_invalid:
      return "data_alignment invalid or does not match expected value";
    case validate_error::capacity_insufficient:
      return "data_capacity smaller than expected for this endpoint";
    default:
      return "unknown layout validation error";
  }
}

validate_error layout_manager::validate_detailed(const control_block* header, size_t expected_capacity,
                                                 uint32_t expected_layout_type, uint32_t expected_data_alignment,
                                                 uint32_t expected_fixed_item_size,
                                                 std::uint64_t expected_schema_id) {
  if (header == nullptr) {
    return validate_error::not_attached;
  }

  if (header->magic != expected_magic) {
    return validate_error::bad_magic;
  }

  int timeout_limit = layout_manager::is_ready_spin_limit;
  while (!header->is_ready.load(std::memory_order_acquire)) {
    XPROC_CPU_PAUSE();
    if (--timeout_limit <= 0) {
      return validate_error::not_ready_timeout;
    }
  }

  if (header->version_major != version_major || header->version_minor != version_minor) {
    return validate_error::version_mismatch;
  }

  if (header->header_size != sizeof(control_block)) {
    return validate_error::header_size_mismatch;
  }

  if (header->layout_type != expected_layout_type) {
    return validate_error::layout_type_mismatch;
  }

  if (header->fixed_item_size != expected_fixed_item_size) {
    return validate_error::fixed_item_size_mismatch;
  }

  if (header->schema_id != expected_schema_id) {
    return validate_error::schema_id_mismatch;
  }

  const uint32_t want_align = expected_data_alignment ? expected_data_alignment : 8u;
  if (header->data_alignment != want_align || !is_power_of_two_uint32(header->data_alignment)) {
    return validate_error::alignment_invalid;
  }

  if (header->data_capacity < expected_capacity) {
    return validate_error::capacity_insufficient;
  }

  return validate_error::ok;
}

bool layout_manager::validate(control_block* header, size_t expected_capacity, uint32_t expected_layout_type,
                              uint32_t expected_data_alignment, uint32_t expected_fixed_item_size,
                              std::uint64_t expected_schema_id) {
  return validate_detailed(header, expected_capacity, expected_layout_type, expected_data_alignment,
                           expected_fixed_item_size, expected_schema_id) ==
         validate_error::ok;
}

void layout_manager::_init_header(control_block* header, size_t capacity, uint32_t layout_type,
                                  uint32_t data_alignment, uint32_t fixed_item_size, std::uint64_t schema_id,
                                  std::uint64_t creator_timestamp_ns, std::uint64_t creator_flags) {
  header->magic = expected_magic;
  header->version_major = version_major;
  header->version_minor = version_minor;
  header->header_size = sizeof(control_block);
  header->layout_type = layout_type;

  header->rb_meta.write_pos.store(0, std::memory_order_relaxed);
  header->rb_meta.read_pos.store(0, std::memory_order_relaxed);
  header->rb_meta.commit_seq.store(0, std::memory_order_relaxed);
  header->rb_meta.read_wake_seq.store(0, std::memory_order_relaxed);

  header->data_capacity = capacity;
  header->data_alignment = data_alignment ? data_alignment : 8u;
  header->fixed_item_size = fixed_item_size;
  header->schema_id = schema_id;
  header->creator_timestamp_ns = creator_timestamp_ns;
  header->creator_flags = creator_flags;
  std::memset(header->reserved, 0, sizeof(header->reserved));

  header->producer_pid.store(xproc::platform::current_process_id(), std::memory_order_relaxed);
  header->attach_count.store(1, std::memory_order_relaxed);

  header->is_ready.store(true, std::memory_order_release);
}

}  // namespace shm
}  // namespace xproc
