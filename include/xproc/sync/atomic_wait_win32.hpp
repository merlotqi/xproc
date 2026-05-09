#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <xproc/platform/platform.hpp>

namespace xproc::sync {
namespace details {

using WaitOnAddressFn = BOOL(WINAPI*)(volatile VOID*, PVOID, SIZE_T, DWORD);
using WakeByAddressFn = VOID(WINAPI*)(PVOID);

struct wait_api {
  WaitOnAddressFn wait_on_address{nullptr};
  WakeByAddressFn wake_one{nullptr};
  WakeByAddressFn wake_all{nullptr};
};

inline wait_api load_wait_api() {
  wait_api api{};
  const HMODULE modules[] = {::GetModuleHandleW(L"kernel32.dll"), ::GetModuleHandleW(L"KernelBase.dll")};
  for (const HMODULE module : modules) {
    if (module == nullptr) {
      continue;
    }
    if (api.wait_on_address == nullptr) {
      api.wait_on_address = reinterpret_cast<WaitOnAddressFn>(::GetProcAddress(module, "WaitOnAddress"));
    }
    if (api.wake_one == nullptr) {
      api.wake_one = reinterpret_cast<WakeByAddressFn>(::GetProcAddress(module, "WakeByAddressSingle"));
    }
    if (api.wake_all == nullptr) {
      api.wake_all = reinterpret_cast<WakeByAddressFn>(::GetProcAddress(module, "WakeByAddressAll"));
    }
  }
  return api;
}

inline const wait_api& native_wait_api() {
  static const wait_api api = load_wait_api();
  return api;
}

inline bool has_native_wait_api() {
  const wait_api& api = native_wait_api();
  return api.wait_on_address != nullptr && api.wake_one != nullptr && api.wake_all != nullptr;
}

template <typename T>
inline volatile void* wait_address(const std::atomic<T>* atomic) {
  return const_cast<volatile void*>(reinterpret_cast<volatile const void*>(atomic));
}

template <typename T>
inline void* wake_address(const std::atomic<T>* atomic) {
  return const_cast<void*>(reinterpret_cast<const void*>(atomic));
}

template <typename T>
inline void polling_wait(const std::atomic<T>* atomic, T old) {
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
      ::SwitchToThread();
    } else {
      ::Sleep(1);
    }
  }
}

}  // namespace details

// WaitOnAddress / WakeByAddress* are native address-wait primitives, but Microsoft documents them
// as same-process synchronization. xproc therefore uses a short timed wait loop: same-process
// waiters wake immediately via WakeByAddress*, while shared-memory waiters in other processes still
// make progress by timing out, reloading the atomic word, and looping.

template <typename T>
inline void atomic_wait(const std::atomic<T>* atomic, T old) {
  static_assert(sizeof(T) == 4, "atomic_wait(win32): only 32-bit atomics are supported");

  if (!details::has_native_wait_api()) {
    details::polling_wait(atomic, old);
    return;
  }

  constexpr DWORD kWaitTimeoutMs = 1;
  const details::wait_api& api = details::native_wait_api();
  while (atomic->load(std::memory_order_acquire) == old) {
    (void)api.wait_on_address(details::wait_address(atomic), &old, sizeof(T), kWaitTimeoutMs);
  }
}

template <typename T>
inline void atomic_notify_one(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_one(win32): only 32-bit atomics are supported");
  if (details::has_native_wait_api()) {
    details::native_wait_api().wake_one(details::wake_address(atomic));
  }
}

template <typename T>
inline void atomic_notify_all(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_all(win32): only 32-bit atomics are supported");
  if (details::has_native_wait_api()) {
    details::native_wait_api().wake_all(details::wake_address(atomic));
  }
}

template void atomic_wait<uint32_t>(const std::atomic<uint32_t>*, uint32_t);
template void atomic_notify_one<uint32_t>(const std::atomic<uint32_t>*);
template void atomic_notify_all<uint32_t>(const std::atomic<uint32_t>*);

}  // namespace sync::xproc

#endif
