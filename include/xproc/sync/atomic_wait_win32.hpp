#pragma once

#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <xproc/platform/platform.hpp>

namespace xproc::sync {

// WaitOnAddress / WakeByAddress match on virtual addresses, not on shared file-mapping bytes. Each
// MapViewOfFile of the same section maps a different VA in the process, so producer and consumer
// threads (or two processes) never wake each other. Use a polling wait with backoff instead.

template <typename T>
inline void atomic_wait(const std::atomic<T>* atomic, T old) {
  static_assert(sizeof(T) == 4, "atomic_wait(win32): only 32-bit atomics are supported");
  std::uint32_t iteration = 0;
  while (true) {
    const T cur = atomic->load(std::memory_order_acquire);
    if (cur != old) {
      return;
    }
    ++iteration;
    if (iteration <= 1000) {
      XPROC_CPU_PAUSE();
    } else if (iteration <= 1010) {
      std::this_thread::yield();
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

template <typename T>
inline void atomic_notify_one(const std::atomic<T>*) {
  // No-op: waiters poll (see atomic_wait).
}

template <typename T>
inline void atomic_notify_all(const std::atomic<T>*) {}

}  // namespace sync::xproc

#endif
