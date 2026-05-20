#pragma once

#include <atomic>

namespace xproc::ringbuffer::detail {

struct fixed_message_header {
  std::atomic<uint32_t> status;
};

}  // namespace xproc::ringbuffer::detail
