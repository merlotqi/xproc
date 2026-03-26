#pragma once

#ifdef _WIN32

#include <atomic>
#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace xproc {
namespace sync {

// WaitOnAddress / WakeByAddress* (Windows 8+). Same 32-bit-only contract as the Linux futex path.
template <typename T>
inline void atomic_wait(const std::atomic<T> *atomic, T old) {
  static_assert(sizeof(T) == 4, "atomic_wait(win32): only 32-bit atomics are supported");
  while (true) {
    const T cur = atomic->load(std::memory_order_acquire);
    if (cur != old) {
      break;
    }
    T compare = old;
    ::WaitOnAddress(const_cast<std::atomic<T> *>(atomic), &compare, sizeof(T), INFINITE);
  }
}

template <typename T>
inline void atomic_notify_one(const std::atomic<T> *atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_one(win32): only 32-bit atomics are supported");
  ::WakeByAddressSingle(const_cast<std::atomic<T> *>(atomic));
}

template <typename T>
inline void atomic_notify_all(const std::atomic<T> *atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_all(win32): only 32-bit atomics are supported");
  ::WakeByAddressAll(const_cast<std::atomic<T> *>(atomic));
}

}  // namespace sync
}  // namespace xproc

#endif
