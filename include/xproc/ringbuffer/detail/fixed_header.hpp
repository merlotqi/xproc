#pragma once

#include <atomic>
#include <xproc/ipc/message_meta.hpp>

namespace xproc::ringbuffer::detail {

struct fixed_message_header {
  std::atomic<uint32_t> status;
  xproc::ipc::message_meta meta;
};

}  // namespace xproc::ringbuffer::detail
