#pragma once

#include "xproc/shm/shm_layout_manager.hpp"
#include "xproc/shm/shm_open_mode.hpp"
#include <atomic>
#include <stdexcept>
#include <unistd.h>
#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout.hpp>
#include <xproc/ipc/ipc_options.hpp>

namespace xproc {
namespace ipc {

class ipc_endpoint {
public:
    enum class role {
        producer,
        comsumer,
        observer
    };

    explicit ipc_endpoint(const transport_options& opts, role rle) : role_(rle), opts_(opts)
    {
        _establish_connection();
    }

    ~ipc_endpoint() {
        if (header_) {
            header_->attach_count.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    ipc_endpoint(const ipc_endpoint&) = delete;
    ipc_endpoint& operator=(const ipc_endpoint&) = delete; 

    role user_role() const { return role_; }
    bool is_connect() const { return header_ != nullptr; }
    shm::shm_control_block* header() const { return header_; }
    transport_options options() const { return opts_; }

protected:
    role role_;
    shm::shm shm_;
    shm::shm_control_block *header_{nullptr};
    transport_options opts_;

private:
    void _establish_connection() {
        using namespace xproc::shm;
        shm_open_mode mode = shm_open_mode::open;
        if (role_ == role::producer && opts_.create_if_missing) {
            mode = shm_open_mode::open_create;
        }

        if (!shm_.open(opts_.path, opts_.shm_size, mode)) {
            throw std::runtime_error("ipc_endpoint: failed to attch shm path: " + opts_.path);
        }

        bool is_creater = (role_ == role::producer);
        
        size_t data_capacity = opts_.shm_size - sizeof(shm_control_block);
        header_ = shm_layout_manager::format(shm_, data_capacity, is_creater);
        if (!header_) {
            throw std::runtime_error("ipc_endpoint: shm layout validation failed.");
        }

        if (role_ == role::producer) {
            header_->producer_pid.store(getpid(), std::memory_order_relaxed);
        }
    }
};

}
}