#pragma once

#include <atomic>

namespace xproc::ringbuffer::details {

struct fixed_message_header {
  std::atomic<uint32_t> status;
};

}  // namespace xproc::ringbuffer::details
