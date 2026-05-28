#pragma once

#include <cstdint>
#include <system_error>
#include <xproc/core/shm.hpp>
#include <xproc/core/shm_layout.hpp>

namespace xproc::core {

// Versions stored in a mapped control block (for logging / compatibility checks before full validate).
struct embedded_version {
  std::uint16_t major{0};
  std::uint32_t minor{0};
};

inline embedded_version read_embedded_version(const control_block* h) noexcept {
  embedded_version v;
  if (!h) {
    return v;
  }
  v.major = h->version_major;
  v.minor = h->version_minor;
  return v;
}

enum class attach_behavior {
  ref_count,  // Non-creator: validate and increment attach_count (producer/consumer IPC).
  readonly    // Non-creator: validate only; read-only maps cannot mutate attach_count.
};

enum class validate_error {
  ok = 0,
  not_attached,
  bad_magic,
  not_ready_timeout,
  version_mismatch,
  header_size_mismatch,
  layout_type_mismatch,
  fixed_item_size_mismatch,
  schema_id_mismatch,
  alignment_invalid,
  capacity_insufficient,
};

inline const std::error_category& layout_error_category() noexcept {
  class layout_error_category_impl final : public std::error_category {
   public:
    const char* name() const noexcept override { return "xproc.layout"; }
    std::string message(int ev) const override {
      switch (static_cast<validate_error>(ev)) {
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
  };
  static layout_error_category_impl instance;
  return instance;
}

inline std::error_code make_error_code(validate_error e) noexcept {
  return {static_cast<int>(e), layout_error_category()};
}

class layout_manager {
 public:
  // Spin iterations waiting for creator to set is_ready (best-effort; see docs/design.md).
  static constexpr int is_ready_spin_limit = 1'000'000;

  static constexpr uint32_t expected_magic = 0x58505243;
  static constexpr uint16_t version_major = 0;
  static constexpr uint32_t version_minor = 3;

  static control_block* format(shm& sm, size_t capacity, bool is_creator, uint32_t layout_type, uint32_t data_alignment,
                               uint32_t fixed_item_size = 0u, std::uint64_t expected_schema_id = 0u,
                               std::uint64_t creator_timestamp_ns = 0u, std::uint64_t creator_flags = 0u,
                               attach_behavior behavior = attach_behavior::ref_count);

  static control_block* format(shm& sm, size_t capacity, bool is_creator, uint32_t layout_type, uint32_t data_alignment,
                               uint32_t fixed_item_size, std::uint64_t expected_schema_id, attach_behavior behavior) {
    return format(sm, capacity, is_creator, layout_type, data_alignment, fixed_item_size, expected_schema_id, 0u, 0u,
                  behavior);
  }

  static bool validate(control_block* header, size_t expected_capacity, uint32_t expected_layout_type,
                       uint32_t expected_data_alignment, uint32_t expected_fixed_item_size = 0u,
                       std::uint64_t expected_schema_id = 0u);

  static validate_error validate_detailed(const control_block* header, size_t expected_capacity,
                                          uint32_t expected_layout_type, uint32_t expected_data_alignment,
                                          uint32_t expected_fixed_item_size = 0u,
                                          std::uint64_t expected_schema_id = 0u);

  static const char* validate_cstr(validate_error e) noexcept;

 private:
  static void _init_header(control_block* header, size_t capacity, uint32_t layout_type, uint32_t data_alignment,
                           uint32_t fixed_item_size, std::uint64_t schema_id, std::uint64_t creator_timestamp_ns,
                           std::uint64_t creator_flags);
};

}  // namespace xproc::core

namespace std {
template <>
struct is_error_code_enum<xproc::core::validate_error> : true_type {};
}  // namespace std
