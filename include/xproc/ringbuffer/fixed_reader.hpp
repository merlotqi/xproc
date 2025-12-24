#pragma once

#include <atomic>
#include <cstdint>
#include <xproc/ringbuffer/details/fixed_header.hpp>
#include <xproc/ringbuffer/ringbuffer_view.hpp>
#include <xproc/sync/atomic_backoff.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc {
namespace ringbuffer {

class fixed_reader : public ringbuffer_view
{
public:
    using ringbuffer_view::ringbuffer_view;

    template<typename F>
    bool try_read(uint32_t item_size, F &&handler)
    {
        uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_relaxed);
        uint32_t total_len = align_size(item_size + sizeof(details::fixed_message_header));

        auto *h = reinterpret_cast<details::fixed_message_header *>(get_ptr(curr_read));
        if (h->status.load(std::memory_order_acquire) == 1)
        {
            handler(get_ptr(curr_read + sizeof(details::fixed_message_header)));
            h->status.store(0, std::memory_order_relaxed);
            header_->rb_meta.read_pos.store(curr_read + total_len, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    sync::atomic_backoff backoff_;
};

}// namespace ringbuffer
}// namespace xproc
