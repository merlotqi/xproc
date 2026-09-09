#include <atomic>
#include <cstring>
#include <xproc/core/layout_manager.hpp>
#include <xproc/core/layout_types.hpp>
#include <xproc/platform/platform.hpp>
#include <xproc/platform/process.hpp>

namespace xproc::core {

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
                                                 uint32_t expected_fixed_item_size, std::uint64_t expected_schema_id) {
  if (header == nullptr) {
    return validate_error::not_attached;
  }

  // Validate ring-level fields. We can't use spscring::validate_control_block() directly because
  // xproc's control_block is larger (header_size differs from spscring's sizeof).
  if (header->spscring_cb.magic != expected_magic) {
    return validate_error::bad_magic;
  }

  int timeout_limit = layout_manager::is_ready_spin_limit;
  while (!header->is_ready.load(std::memory_order_acquire)) {
    XPROC_CPU_PAUSE();
    if (--timeout_limit <= 0) {
      return validate_error::not_ready_timeout;
    }
  }

  if (header->spscring_cb.version_major != version_major || header->spscring_cb.version_minor != version_minor) {
    return validate_error::version_mismatch;
  }

  if (header->spscring_cb.header_size != sizeof(control_block)) {
    return validate_error::header_size_mismatch;
  }

  if (header->spscring_cb.layout_type != expected_layout_type) {
    return validate_error::layout_type_mismatch;
  }

  if (header->spscring_cb.fixed_item_size != expected_fixed_item_size) {
    return validate_error::fixed_item_size_mismatch;
  }

  if (header->schema_id != expected_schema_id) {
    return validate_error::schema_id_mismatch;
  }

  const uint32_t want_align = expected_data_alignment ? expected_data_alignment : 8u;
  if (header->spscring_cb.data_alignment != want_align || !is_power_of_two_uint32(header->spscring_cb.data_alignment)) {
    return validate_error::alignment_invalid;
  }

  if (header->spscring_cb.data_capacity < expected_capacity) {
    return validate_error::capacity_insufficient;
  }

  return validate_error::ok;
}

bool layout_manager::validate(control_block* header, size_t expected_capacity, uint32_t expected_layout_type,
                              uint32_t expected_data_alignment, uint32_t expected_fixed_item_size,
                              std::uint64_t expected_schema_id) {
  return validate_detailed(header, expected_capacity, expected_layout_type, expected_data_alignment,
                           expected_fixed_item_size, expected_schema_id) == validate_error::ok;
}

void layout_manager::_init_header(control_block* header, size_t capacity, uint32_t layout_type, uint32_t data_alignment,
                                  uint32_t fixed_item_size, std::uint64_t schema_id, std::uint64_t creator_timestamp_ns,
                                  std::uint64_t creator_flags) {
  // Initialize the ring part via spscring.
  auto layout = static_cast<spscring::layout_type>(layout_type);
  spscring::init_control_block(header->spscring_cb, capacity, layout, data_alignment ? data_alignment : 8u,
                               fixed_item_size);

  // Override header_size to cover the full xproc control_block.
  header->spscring_cb.header_size = sizeof(control_block);

  // xproc-specific fields.
  header->schema_id = schema_id;
  header->creator_timestamp_ns = creator_timestamp_ns;
  header->creator_flags = creator_flags;
  std::memset(header->reserved_xproc, 0, sizeof(header->reserved_xproc));

  header->producer_pid.store(xproc::platform::current_process_id(), std::memory_order_relaxed);
  header->attach_count.store(1, std::memory_order_relaxed);

  header->is_ready.store(true, std::memory_order_release);
}

}  // namespace xproc::core
