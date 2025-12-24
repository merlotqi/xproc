#pragma once

#include "xproc/ipc/ipc_options.hpp"
#include <cstring>
#include <xproc/ipc/ipc_endpoint.hpp>
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/fixed_writer.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/ringbuffer/varlen_writer.hpp>
#include <variant>
#include <memory>

namespace xproc {
namespace ipc {

class ipc_channel : public ipc_endpoint
{
public:
    using ipc_endpoint::ipc_endpoint;

    void init_view() {
    if (opts_.type == channel_type::fixed) {    
            writer_ = std::make_unique<ringbuffer::fixed_writer>(header_);
            reader_ = std::make_unique<ringbuffer::fixed_reader>(header_);
        } else {
            writer_ = std::make_unique<ringbuffer::varlen_writer>(header_);
            reader_ = std::make_unique<ringbuffer::varlen_reader>(header_);
        }
    }

    template<typename T>
    void send_fixed(const T& data) {
        auto *fw = static_cast<ringbuffer::fixed_writer *>(writer_.get());
        uint64_t pos;
        void* buf = fw->reserve(sizeof(T), pos);
        std::memcpy(buf, &data, sizeof(T));
        fw->commit(pos);
    }

    void send_varlen(const void* data, uint32_t len) {
        auto* vw = static_cast<ringbuffer::varlen_writer*>(writer_.get());
        uint64_t pos;
        void* buf = vw->reserve(len, pos);
        std::memcpy(buf, data, len);
        vw->commit(pos);
    }

    template<typename F>
    bool poll(F&& handler) {
        if (opts_.type == channel_type::fixed) {
            auto* fr = static_cast<ringbuffer::fixed_reader*>(reader_.get());
            return fr->try_read(opts_.item_size, std::forward<F>(handler));
        } else {
            auto* vr = static_cast<ringbuffer::varlen_reader*>(reader_.get());
            return vr->try_read(std::forward<F>(handler));
        }
    }

private:
    std::unique_ptr<ringbuffer::ringbuffer_view> writer_;
    std::unique_ptr<ringbuffer::ringbuffer_view> reader_;
};

}
}