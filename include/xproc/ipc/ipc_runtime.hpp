#pragma once

#include <vector>
#include <xproc/ipc/ipc_channel.hpp>
#include <atomic>
#include <thread>
#include <functional>

namespace xproc {
namespace ipc {

class ipc_runtime {
public:
    explicit ipc_runtime(ipc_channel &channel) : channel_(channel) {}

    template<typename Executor>
    void run(Executor&& pool_executor)
    {
        running_.store(true);
        while (running_.load(std::memory_order_relaxed)) {
            bool has_data = channel_.poll([&](void* ptr, uint32_t len = 0){
                std::vector<uint8_t> copy_data(static_cast<uint8_t*>(ptr), static_cast<uint8_t*>(ptr) + (len ?  len : channel_.options().item_size));
                
                pool_executor([data = std::move(copy_data)](){
                    // processData
                });
            });

            if (!has_data) {
                uint64_t last_pos = channel_.header()->rb_meta.write_pos.load(std::memory_order_acquire);
                sync::atomic_wait(&channel_.header()->rb_meta.write_pos, last_pos);
            }
        }
    }

    void stop() { running_.store(false); }

private:
    ipc_channel& channel_;
    std::atomic_bool running_{false};
};

}
}