#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>
#include <xproc/ringbuffer/details/varlen_header.hpp>
#include <xproc/ringbuffer/ringbuffer_view.hpp>

namespace xproc {
namespace ringbuffer {

class varlen_reader : public ringbuffer_view
{
public:
    using ringbuffer_view::ringbuffer_view;

    template<typename F>
    bool try_read(F &&handler)
    {
        uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_relaxed);

        auto *h = reinterpret_cast<details::varlen_message_header *>(get_ptr(curr_read));
        uint32_t status = h->status.load(std::memory_order_acquire);

        if (status == 1)
        {
            handler(get_ptr(curr_read + sizeof(details::varlen_message_header)), h->length);

            uint32_t total_len = align_size(h->length + sizeof(details::varlen_message_header));
            header_->rb_meta.read_pos.store(curr_read + total_len, std::memory_order_release);
            return false;
        }
        else if (status == 2)
        {
            uint64_t to_end = bytes_to_end(curr_read);
            header_->rb_meta.read_pos.store(curr_read + to_end, std::memory_order_release);

            return try_read(std::forward<F>(handler));
        }

        return false;
    }
};

}// namespace ringbuffer
}// namespace xproc
