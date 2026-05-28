#pragma once

#ifdef __linux__

#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <climits>
#include <cstdint>

namespace xproc::sync {
namespace details {

inline long futex(uint32_t* uaddr, int futex_op, uint32_t val, const struct timespec* timeout, uint32_t* uaddr2,
                  uint32_t val3) {
  return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

}  // namespace details

// Linux futex compare value is 32-bit. Do not use with 64-bit atomics (high bits would be truncated).
// Wait words in MAP_SHARED memory must not use FUTEX_PRIVATE_FLAG so wake reaches other processes.
template <typename T>
inline void atomic_wait(const std::atomic<T>* atomic, T old) {
  static_assert(sizeof(T) == 4, "atomic_wait(futex): only 32-bit atomics are supported");
  while (atomic->load(std::memory_order_acquire) == old) {
    details::futex(reinterpret_cast<uint32_t*>(const_cast<std::atomic<T>*>(atomic)), FUTEX_WAIT,
                   static_cast<uint32_t>(old), nullptr, nullptr, 0);
  }
}

template <typename T>
inline void atomic_notify_one(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_one(futex): only 32-bit atomics are supported");
  details::futex(reinterpret_cast<uint32_t*>(const_cast<std::atomic<T>*>(atomic)), FUTEX_WAKE, 1, nullptr, nullptr, 0);
}

template <typename T>
inline void atomic_notify_all(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_all(futex): only 32-bit atomics are supported");
  details::futex(reinterpret_cast<uint32_t*>(const_cast<std::atomic<T>*>(atomic)), FUTEX_WAKE, INT_MAX, nullptr,
                 nullptr, 0);
}

template void atomic_wait<uint32_t>(const std::atomic<uint32_t>*, uint32_t);
template void atomic_notify_one<uint32_t>(const std::atomic<uint32_t>*);
template void atomic_notify_all<uint32_t>(const std::atomic<uint32_t>*);

}  // namespace xproc::sync

#endif
