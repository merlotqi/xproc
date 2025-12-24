#pragma once

#include <bits/types/struct_timespec.h>
#include <climits>
#include <cstddef>
#include <cstdint>

#ifdef __linux__

#include <atomic>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace xproc {
namespace sync {
namespace details {

inline long futex(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2,
                  uint32_t val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

}// namespace details

template<typename T>
inline void atomic_wait(const std::atomic<T> *atomic, T old)
{
    while (atomic->load(std::memory_order_relaxed) == old)
    {
        details::futex(reinterpret_cast<uint32_t *>(const_cast<std::atomic<T> *>(atomic)), FUTEX_WAIT,
                       static_cast<uint32_t>(old), NULL, NULL, 0);
    }
}

template<typename T>
inline void atomic_notify_one(const std::atomic<T> *atomic)
{
    details::futex(reinterpret_cast<uint32_t *>(const_cast<std::atomic<T> *>(atomic)), FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                   1, nullptr, nullptr, 0);
}

template<typename T>
inline void atomic_notify_all(const std::atomic<T> *atomic)
{
    details::futex(reinterpret_cast<uint32_t *>(const_cast<std::atomic<T> *>(atomic)), FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                   INT_MAX, nullptr, nullptr, 0);
}

template void atomic_wait<uint64_t>(const std::atomic<uint64_t> *, uint64_t);
template void atomic_notify_one<uint64_t>(const std::atomic<uint64_t> *);
template void atomic_notify_all<uint64_t>(const std::atomic<uint64_t> *);

template void atomic_wait<uint32_t>(const std::atomic<uint32_t> *, uint32_t);
template void atomic_notify_one<uint32_t>(const std::atomic<uint32_t> *);
template void atomic_notify_all<uint32_t>(const std::atomic<uint32_t> *);

}// namespace sync
}// namespace xproc

#endif
