#pragma once

#include <cstdint>
#include <system_error>
#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout.hpp>

namespace xproc {
namespace shm {

// Versions stored in a mapped control block (for logging / compatibility checks before full validate).
struct embedded_layout_version {
  std::uint16_t major{0};
  std::uint32_t minor{0};
};

inline embedded_layout_version read_embedded_layout_version(const shm_control_block* h) noexcept {
  embedded_layout_version v;
  if (!h) {
    return v;
  }
  v.major = h->version_major;
  v.minor = h->version_minor;
  return v;
}

enum class layout_attach_behavior {
  count_ref,    // Non-creator: validate and increment attach_count (producer/consumer IPC).
  observe_only  // Non-creator: validate only; read-only maps cannot mutate attach_count.
};

enum class layout_validate_error {
  ok = 0,
  not_attached,
  bad_magic,
  not_ready_timeout,
  version_mismatch,
  header_size_mismatch,
  layout_type_mismatch,
  alignment_invalid,
  capacity_insufficient,
};

inline const std::error_category& layout_error_category() noexcept {
  class layout_error_category_impl final : public std::error_category {
   public:
    const char* name() const noexcept override { return "xproc.layout"; }
    std::string message(int ev) const override {
      switch (static_cast<layout_validate_error>(ev)) {
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
  };
  static layout_error_category_impl instance;
  return instance;
}

inline std::error_code make_error_code(layout_validate_error e) noexcept {
  return {static_cast<int>(e), layout_error_category()};
}

class shm_layout_manager {
 public:
  // Spin iterations waiting for creator to set is_ready (best-effort; see docs/design.md).
  static constexpr int is_ready_spin_limit_v = 1'000'000;

  static constexpr uint32_t EXPECTED_MAGIC = 0x58505243;
  static constexpr uint16_t VERSION_MAJOR = 0;
  static constexpr uint32_t VERSION_MINOR = 2;

  static shm_control_block* format(shm& sm, size_t capacity, bool is_creator, uint32_t layout_type,
                                   uint32_t data_alignment,
                                   layout_attach_behavior behavior = layout_attach_behavior::count_ref);

  static bool validate(shm_control_block* header, size_t expected_capacity, uint32_t expected_layout_type,
                       uint32_t expected_data_alignment);

  static layout_validate_error validate_detailed(const shm_control_block* header, size_t expected_capacity,
                                                 uint32_t expected_layout_type, uint32_t expected_data_alignment);

  static const char* layout_validate_cstr(layout_validate_error e) noexcept;

 private:
  static void _init_header(shm_control_block* header, size_t capacity, uint32_t layout_type, uint32_t data_alignment);
};

}  // namespace shm
}  // namespace xproc

namespace std {
template <>
struct is_error_code_enum<xproc::shm::layout_validate_error> : true_type {};
}  // namespace std
