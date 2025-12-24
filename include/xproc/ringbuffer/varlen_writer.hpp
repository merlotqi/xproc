#pragma once

#include <atomic>
#include <cstdint>
#include <xproc/ringbuffer/details/varlen_header.hpp>
#include <xproc/ringbuffer/ringbuffer_view.hpp>
#include <xproc/sync/atomic_backoff.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc {
namespace ringbuffer {

class varlen_writer : public ringbuffer_view
{
public:
    using ringbuffer_view::ringbuffer_view;

    void *reserve(uint32_t len, uint64_t &out_pos)
    {
        const uint32_t total_len = align_size(len + sizeof(details::varlen_message_header));

        while (true)
        {
            uint64_t curr_write = header_->rb_meta.write_pos.load(std::memory_order_relaxed);
            uint64_t curr_read = header_->rb_meta.read_pos.load(std::memory_order_acquire);

            if (curr_write + total_len - curr_read > header_->data_capacity)
            {
                backoff_.pause(header_->rb_meta.read_pos, curr_read);
                continue;
            }

            uint64_t to_end = bytes_to_end(curr_write);
            if (to_end < total_len)
            {
                if (header_->rb_meta.write_pos.compare_exchange_strong(curr_write, curr_write + to_end))
                {
                    auto *h = reinterpret_cast<details::varlen_message_header *>(get_ptr(curr_write));
                    h->status.store(2, std::memory_order_release);// 2 = Dummy
                }
                continue;
            }

            if (header_->rb_meta.write_pos.compare_exchange_strong(curr_write, curr_write + total_len))
            {
                out_pos = curr_write;
                auto *h = reinterpret_cast<details::varlen_message_header *>(get_ptr(out_pos));
                h->length = len;
                h->status.store(0, std::memory_order_relaxed);

                return get_ptr(out_pos + sizeof(details::varlen_message_header));
            }
        }
    }

    void commit(uint64_t pos)
    {
        auto *h = reinterpret_cast<details::varlen_message_header *>(get_ptr(pos));
        h->status.store(1, std::memory_order_release);// 1 = Ready

        sync::atomic_notify_one(&header_->rb_meta.write_pos);
    }

private:
    sync::atomic_backoff backoff_;
};

}// namespace ringbuffer
}// namespace xproc
