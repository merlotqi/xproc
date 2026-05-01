#pragma once

#ifdef __APPLE__

#include <stdint.h>

#include <atomic>
#include <climits>
#include <cstdint>

// Darwin __ulock_wait / __ulock_wake — kernel primitives analogous to Linux futex.
// On arm64 Apple Silicon these trap directly into the kernel with minimal overhead.
// Exported from libsystem_kernel.dylib; declared here to avoid the deprecated
// syscall(2) wrapper.
//
// Caveat: unlike Linux futex (which translates VA → physical page internally),
// __ulock operates on virtual addresses. For MAP_SHARED memory mapped at different
// VA in two processes, the kernel wait may return -EINVAL on the wake side. We
// handle this with a bounded kernel-wait timeout (50 ms) followed by a user-space
// value re-check, so the waiter always makes progress even when producer and
// consumer live in separate processes.

#define XPROC_ULOCK_WAIT_OP      0x00000100  // UL_COMPARE_AND_WAIT
#define XPROC_ULOCK_WAIT_SHARED  0x00000101  // UL_COMPARE_AND_WAIT_SHARED
#define XPROC_ULOCK_WAKE_OP      0x00000100  // UL_WAKE
#define XPROC_ULOCK_WAKE_SHARED  0x00000101
#define XPROC_ULOCK_WAKE_ALL     0x10000100  // UL_WAKE | UL_WAKE_ALL bit

#define XPROC_ULOCK_TIMEOUT_NS   50000000ULL  // 50 ms — bounds worst-case cross-process latency

extern "C" int __ulock_wait(uint32_t operation, void* addr, uint64_t value, uint64_t timeout);
extern "C" int __ulock_wake(uint32_t operation, void* addr, uint64_t wake_value);

namespace xproc {
namespace sync {

// Wait until *atomic != old. Uses kernel sleep when possible; falls back to
// polling on cross-process address mismatch (EINVAL from ulock).
template <typename T>
inline void atomic_wait(const std::atomic<T>* atomic, T old) {
  static_assert(sizeof(T) == 4, "atomic_wait(darwin): only 32-bit atomics are supported");
  while (atomic->load(std::memory_order_acquire) == old) {
    int r = __ulock_wait(XPROC_ULOCK_WAIT_OP,
                         const_cast<std::atomic<T>*>(atomic),
                         static_cast<uint64_t>(static_cast<uint32_t>(old)),
                         XPROC_ULOCK_TIMEOUT_NS);
    if (r == 0) {
      // Kernel detected value change — re-check to handle spurious wake.
      continue;
    }
    // -ETIMEDOUT: periodic re-check (normal bounded-wait path).
    // -EINVAL / -EFAULT: address not tracked by kernel (cross-process VA mismatch)
    //   — spin back through the while-loop to re-read the value.
    // -EINTR: interrupted by signal — retry.
    (void)r;
  }
}

// Wake one waiter sleeping on this address via __ulock_wake.
// No-op when no waiters are registered (returns -ENOENT) — harmless.
template <typename T>
inline void atomic_notify_one(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_one(darwin): only 32-bit atomics are supported");
  __ulock_wake(XPROC_ULOCK_WAKE_OP,
               const_cast<std::atomic<T>*>(atomic),
               0);
}

// Wake all waiters sleeping on this address.
template <typename T>
inline void atomic_notify_all(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_all(darwin): only 32-bit atomics are supported");
  __ulock_wake(XPROC_ULOCK_WAKE_ALL,
               const_cast<std::atomic<T>*>(atomic),
               0);
}

template void atomic_wait<uint32_t>(const std::atomic<uint32_t>*, uint32_t);
template void atomic_notify_one<uint32_t>(const std::atomic<uint32_t>*);
template void atomic_notify_all<uint32_t>(const std::atomic<uint32_t>*);

}  // namespace sync
}  // namespace xproc

#endif
