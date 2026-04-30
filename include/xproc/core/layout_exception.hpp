#pragma once

#include <stdexcept>
#include <string>
#include <system_error>
#include <xproc/core/shm_layout_manager.hpp>

namespace xproc::core {

// Thrown when format/validate fails; carries validate_error for programmatic handling.
class layout_exception : public std::runtime_error {
 public:
  explicit layout_exception(std::string prefix, validate_error code)
      : std::runtime_error(prefix + layout_manager::validate_cstr(code)), code_(code) {}

  validate_error code() const noexcept { return code_; }
  std::error_code ec() const noexcept { return make_error_code(code_); }

 private:
  validate_error code_;
};

}  // namespace core::xproc
