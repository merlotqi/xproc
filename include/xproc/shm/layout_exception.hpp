#pragma once

#include <stdexcept>
#include <string>
#include <xproc/shm/shm_layout_manager.hpp>

namespace xproc {
namespace shm {

// Thrown when format/validate fails; carries layout_validate_error for programmatic handling.
class layout_exception : public std::runtime_error {
 public:
  explicit layout_exception(std::string prefix, layout_validate_error code)
      : std::runtime_error(prefix + shm_layout_manager::layout_validate_cstr(code)), code_(code) {}

  layout_validate_error code() const noexcept { return code_; }

 private:
  layout_validate_error code_;
};

}  // namespace shm
}  // namespace xproc
