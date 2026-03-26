#include <atomic>
#include <cstring>
#include <xproc/platform/platform.hpp>
#include <xproc/platform/process.hpp>
#include <xproc/shm/shm_layout_manager.hpp>

#include "xproc/shm/shm_layout.hpp"

namespace xproc {
namespace shm {

shm_control_block *shm_layout_manager::format(shm &sm, size_t capacity, bool is_creator, uint32_t layout_type,
                                              uint32_t data_alignment, layout_attach_behavior behavior) {
  if (!sm.is_attached()) {
    return nullptr;
  }

  auto *header = static_cast<shm_control_block *>(sm.addr());
  if (is_creator) {
    _init_header(header, capacity, layout_type, data_alignment);
  } else {
    if (!validate(header, capacity, layout_type, data_alignment)) {
      return nullptr;
    }
    if (behavior == layout_attach_behavior::count_ref) {
      header->attach_count.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return header;
}

namespace {

bool is_power_of_two_uint32(uint32_t x) { return x >= 4u && (x & (x - 1u)) == 0u; }

}  // namespace

const char *shm_layout_manager::layout_validate_cstr(layout_validate_error e) noexcept {
  switch (e) {
    case layout_validate_error::ok:
      return "ok";
    case layout_validate_error::not_attached:
      return "shared memory mapping is not attached";
    case layout_validate_error::bad_magic:
      return "bad magic (not an xproc segment or corrupted)";
    case layout_validate_error::not_ready_timeout:
      return "control block not ready (timeout waiting for is_ready)";
    case layout_validate_error::version_mismatch:
      return "layout version mismatch";
    case layout_validate_error::header_size_mismatch:
      return "header_size does not match this build";
    case layout_validate_error::layout_type_mismatch:
      return "layout_type mismatch (fixed vs variable)";
    case layout_validate_error::alignment_invalid:
      return "data_alignment invalid or does not match expected value";
    case layout_validate_error::capacity_insufficient:
      return "data_capacity smaller than expected for this endpoint";
    default:
      return "unknown layout validation error";
  }
}

layout_validate_error shm_layout_manager::validate_detailed(const shm_control_block *header, size_t expected_capacity,
                                                            uint32_t expected_layout_type,
                                                            uint32_t expected_data_alignment) {
  if (header == nullptr) {
    return layout_validate_error::not_attached;
  }

  if (header->magic != EXPECTED_MAGIC) {
    return layout_validate_error::bad_magic;
  }

  int timeout_limit = shm_layout_manager::is_ready_spin_limit_v;
  while (!header->is_ready.load(std::memory_order_acquire)) {
    XPROC_CPU_PAUSE();
    if (--timeout_limit <= 0) {
      return layout_validate_error::not_ready_timeout;
    }
  }

  if (header->version_major != VERSION_MAJOR || header->version_minor != VERSION_MINOR) {
    return layout_validate_error::version_mismatch;
  }

  if (header->header_size != sizeof(shm_control_block)) {
    return layout_validate_error::header_size_mismatch;
  }

  if (header->layout_type != expected_layout_type) {
    return layout_validate_error::layout_type_mismatch;
  }

  const uint32_t want_align = expected_data_alignment ? expected_data_alignment : 8u;
  if (header->data_alignment != want_align || !is_power_of_two_uint32(header->data_alignment)) {
    return layout_validate_error::alignment_invalid;
  }

  if (header->data_capacity < expected_capacity) {
    return layout_validate_error::capacity_insufficient;
  }

  return layout_validate_error::ok;
}

bool shm_layout_manager::validate(shm_control_block *header, size_t expected_capacity, uint32_t expected_layout_type,
                                  uint32_t expected_data_alignment) {
  return validate_detailed(header, expected_capacity, expected_layout_type, expected_data_alignment) ==
         layout_validate_error::ok;
}

void shm_layout_manager::_init_header(shm_control_block *header, size_t capacity, uint32_t layout_type,
                                      uint32_t data_alignment) {
  header->magic = EXPECTED_MAGIC;
  header->version_major = VERSION_MAJOR;
  header->version_minor = VERSION_MINOR;
  header->header_size = sizeof(shm_control_block);
  header->layout_type = layout_type;

  header->rb_meta.write_pos.store(0, std::memory_order_relaxed);
  header->rb_meta.read_pos.store(0, std::memory_order_relaxed);
  header->rb_meta.commit_seq.store(0, std::memory_order_relaxed);
  header->rb_meta.read_wake_seq.store(0, std::memory_order_relaxed);

  header->data_capacity = capacity;
  header->data_alignment = data_alignment ? data_alignment : 8u;

  header->producer_pid.store(xproc::platform::current_process_id(), std::memory_order_relaxed);
  header->attach_count.store(1, std::memory_order_relaxed);

  header->is_ready.store(true, std::memory_order_release);
}

}  // namespace shm
}  // namespace xproc
