#pragma once

#include <atomic>
#include <xproc/ipc/message_meta.hpp>

namespace xproc::ringbuffer::detail {

struct varlen_message_header {
  std::atomic<uint32_t> status;  // 0: writing, 1: ready, 2: dummy(wrap-around)
  uint32_t length;
  xproc::ipc::message_meta meta;
};

}  // namespace xproc::ringbuffer::detail
