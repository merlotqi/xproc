#pragma once

#include <atomic>

namespace xproc {
namespace ringbuffer {
namespace details {

struct fixed_message_header
{
    std::atomic<uint32_t> status;
};

}// namespace details
}// namespace ringbuffer
}// namespace xproc
