#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <xproc/shm/shm_layout.hpp>

namespace xproc {
namespace ipc {

enum class channel_type {
  fixed,
  variable
};

struct transport_options {
  std::string path;
  size_t shm_size;
  uint32_t item_size = 0;
  uint32_t data_align = 0;
  bool create_if_missing = true;
  channel_type type = channel_type::fixed;
};

// Central validation for ipc_endpoint / ipc_channel / ipc_observer. Throws std::invalid_argument.
inline void validate_transport_options(const transport_options &opts) {
  if (opts.path.empty()) {
    throw std::invalid_argument("transport_options: path must be non-empty");
  }
  constexpr std::size_t k_min = sizeof(shm::shm_control_block);
  if (opts.shm_size < k_min) {
    throw std::invalid_argument("transport_options: shm_size is smaller than shm_control_block");
  }
  if (opts.type == channel_type::fixed && opts.item_size == 0) {
    throw std::invalid_argument("transport_options: fixed channel requires non-zero item_size");
  }
  if (opts.data_align != 0) {
    const uint32_t a = opts.data_align;
    if (a < 4u || (a & (a - 1u)) != 0u) {
      throw std::invalid_argument("transport_options: data_align must be 0 (default 8) or a power of two >= 4");
    }
  }
}

}  // namespace ipc
}  // namespace xproc
