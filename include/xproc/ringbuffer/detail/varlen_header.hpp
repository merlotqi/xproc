#pragma once

#include <atomic>

namespace xproc::ringbuffer::detail {

struct varlen_message_header {
  std::atomic<uint32_t> status;  // 0: writing, 1: ready, 2: dummy(wrap-around)
  uint32_t length;
};

}  // namespace xproc::ringbuffer::detail
