#pragma once

#include <atomic>

namespace xproc {
namespace sync {

template <typename T>
void atomic_wait(const std::atomic<T>* atomic, T old);

template <typename T>
void atomic_notify_one(const std::atomic<T>* atomic);

template <typename T>
void atomic_notify_all(const std::atomic<T>* atomic);

}  // namespace sync
}  // namespace xproc

#if defined(__linux__)
#include <xproc/sync/atomic_wait_futex.hpp>
#elif defined(_WIN32)
#include <xproc/sync/atomic_wait_win32.hpp>
#endif
