#pragma once

#include <cstdint>
#include <thread>
#include <xproc/platform/platform.hpp>
#include <xproc/sync/atomic_wait_futex.hpp>

namespace xproc {
namespace sync {

class atomic_backoff
{
public:
    static constexpr uint32_t default_spin_threshold = 1000;
    static constexpr uint32_t yield_threshold = 10;

    explicit atomic_backoff(uint32_t spin_threshold = default_spin_threshold)
        : spin_threshold_(spin_threshold), iterations_(0)
    {
    }
    template<typename T>
    void pause(const std::atomic<T> &atomic, T old)
    {
        iterations_++;
        if (iterations_ <= spin_threshold_)
        {
            XPROC_CPU_PAUSE();
        }
        else if (iterations_ <= spin_threshold_ + yield_threshold)
        {
            std::this_thread::yield();
        }
        else
        {
            atomic_wait(&atomic, old);
            reset();
        }
    }

    void reset()
    {
        iterations_ = 0;
    }

private:
    uint32_t spin_threshold_;
    uint32_t iterations_;
};

}// namespace sync
}// namespace xproc
