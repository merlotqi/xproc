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
#include <cwchar>
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

struct wait_tuning {
  std::uint32_t spin_count{1000};
  std::uint32_t yield_count{10};
  DWORD wait_timeout_ms{1};
  std::uint32_t polling_sleep_ms{1};
};

struct wait_stats_counters {
  std::atomic<std::uint64_t> wait_calls{0};
  std::atomic<std::uint64_t> polling_wait_calls{0};
  std::atomic<std::uint64_t> native_wait_calls{0};
  std::atomic<std::uint64_t> native_timeout_count{0};
  std::atomic<std::uint64_t> spin_iterations{0};
  std::atomic<std::uint64_t> yield_iterations{0};
  std::atomic<std::uint64_t> polling_sleep_calls{0};
  std::atomic<std::uint64_t> notify_one_calls{0};
  std::atomic<std::uint64_t> notify_all_calls{0};
};

inline wait_stats_counters& global_wait_stats() {
  static wait_stats_counters counters{};
  return counters;
}

inline bool try_read_env_u32(const wchar_t* name, std::uint32_t* out) {
  if (out == nullptr) {
    return false;
  }
  wchar_t buf[32] = {};
  const DWORD len = ::GetEnvironmentVariableW(name, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  if (len == 0 || len >= static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]))) {
    return false;
  }
  wchar_t* end = nullptr;
  const unsigned long parsed = std::wcstoul(buf, &end, 10);
  if (end == buf || *end != L'\0') {
    return false;
  }
  *out = static_cast<std::uint32_t>(parsed);
  return true;
}

inline std::uint32_t clamp_u32(std::uint32_t value, std::uint32_t min_value, std::uint32_t max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

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

inline wait_tuning load_wait_tuning() {
  wait_tuning tuning{};

  std::uint32_t value = 0;
  if (try_read_env_u32(L"XPROC_WIN32_WAIT_SPIN", &value)) {
    tuning.spin_count = clamp_u32(value, 0, 1000000);
  }
  if (try_read_env_u32(L"XPROC_WIN32_WAIT_YIELD", &value)) {
    tuning.yield_count = clamp_u32(value, 0, 100000);
  }
  if (try_read_env_u32(L"XPROC_WIN32_WAIT_TIMEOUT_MS", &value)) {
    tuning.wait_timeout_ms = static_cast<DWORD>(clamp_u32(value, 0, 60000));
  }
  if (try_read_env_u32(L"XPROC_WIN32_POLL_SLEEP_MS", &value)) {
    tuning.polling_sleep_ms = clamp_u32(value, 0, 60000);
  }

  return tuning;
}

inline const wait_tuning& native_wait_tuning() {
  static const wait_tuning tuning = load_wait_tuning();
  return tuning;
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
  const wait_tuning& tuning = native_wait_tuning();
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
      global_wait_stats().polling_sleep_calls.fetch_add(1, std::memory_order_relaxed);
      ::Sleep(tuning.polling_sleep_ms);
    }
  }
}

}  // namespace details

struct atomic_wait_win32_stats {
  std::uint64_t wait_calls{0};
  std::uint64_t polling_wait_calls{0};
  std::uint64_t native_wait_calls{0};
  std::uint64_t native_timeout_count{0};
  std::uint64_t spin_iterations{0};
  std::uint64_t yield_iterations{0};
  std::uint64_t polling_sleep_calls{0};
  std::uint64_t notify_one_calls{0};
  std::uint64_t notify_all_calls{0};
};

inline atomic_wait_win32_stats atomic_wait_win32_get_stats() {
  const details::wait_stats_counters& s = details::global_wait_stats();
  atomic_wait_win32_stats out{};
  out.wait_calls = s.wait_calls.load(std::memory_order_relaxed);
  out.polling_wait_calls = s.polling_wait_calls.load(std::memory_order_relaxed);
  out.native_wait_calls = s.native_wait_calls.load(std::memory_order_relaxed);
  out.native_timeout_count = s.native_timeout_count.load(std::memory_order_relaxed);
  out.spin_iterations = s.spin_iterations.load(std::memory_order_relaxed);
  out.yield_iterations = s.yield_iterations.load(std::memory_order_relaxed);
  out.polling_sleep_calls = s.polling_sleep_calls.load(std::memory_order_relaxed);
  out.notify_one_calls = s.notify_one_calls.load(std::memory_order_relaxed);
  out.notify_all_calls = s.notify_all_calls.load(std::memory_order_relaxed);
  return out;
}

inline void atomic_wait_win32_reset_stats() {
  details::wait_stats_counters& s = details::global_wait_stats();
  s.wait_calls.store(0, std::memory_order_relaxed);
  s.polling_wait_calls.store(0, std::memory_order_relaxed);
  s.native_wait_calls.store(0, std::memory_order_relaxed);
  s.native_timeout_count.store(0, std::memory_order_relaxed);
  s.spin_iterations.store(0, std::memory_order_relaxed);
  s.yield_iterations.store(0, std::memory_order_relaxed);
  s.polling_sleep_calls.store(0, std::memory_order_relaxed);
  s.notify_one_calls.store(0, std::memory_order_relaxed);
  s.notify_all_calls.store(0, std::memory_order_relaxed);
}

// WaitOnAddress / WakeByAddress* are native address-wait primitives, but Microsoft documents them
// as same-process synchronization. xproc therefore uses a three-stage wait: spin, yield, then a
// short timed wait. Nearby same-process hand-offs usually complete in the spin/yield window, and
// cross-process shared-memory hand-offs avoid falling into a coarse kernel timeout on every round.

template <typename T>
inline void atomic_wait(const std::atomic<T>* atomic, T old) {
  static_assert(sizeof(T) == 4, "atomic_wait(win32): only 32-bit atomics are supported");
  details::global_wait_stats().wait_calls.fetch_add(1, std::memory_order_relaxed);

  if (!details::has_native_wait_api()) {
    details::global_wait_stats().polling_wait_calls.fetch_add(1, std::memory_order_relaxed);
    details::polling_wait(atomic, old);
    return;
  }

  const details::wait_api& api = details::native_wait_api();
  const details::wait_tuning& tuning = details::native_wait_tuning();
  while (atomic->load(std::memory_order_acquire) == old) {
    for (std::uint32_t spin = 0; spin < tuning.spin_count; ++spin) {
      details::global_wait_stats().spin_iterations.fetch_add(1, std::memory_order_relaxed);
      XPROC_CPU_PAUSE();
      if (atomic->load(std::memory_order_acquire) != old) {
        return;
      }
    }
    for (std::uint32_t yield = 0; yield < tuning.yield_count; ++yield) {
      details::global_wait_stats().yield_iterations.fetch_add(1, std::memory_order_relaxed);
      ::SwitchToThread();
      if (atomic->load(std::memory_order_acquire) != old) {
        return;
      }
    }
    details::global_wait_stats().native_wait_calls.fetch_add(1, std::memory_order_relaxed);
    const BOOL ok = api.wait_on_address(details::wait_address(atomic), &old, sizeof(T), tuning.wait_timeout_ms);
    if (!ok && ::GetLastError() == ERROR_TIMEOUT) {
      details::global_wait_stats().native_timeout_count.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

template <typename T>
inline void atomic_notify_one(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_one(win32): only 32-bit atomics are supported");
  details::global_wait_stats().notify_one_calls.fetch_add(1, std::memory_order_relaxed);
  if (details::has_native_wait_api()) {
    details::native_wait_api().wake_one(details::wake_address(atomic));
  }
}

template <typename T>
inline void atomic_notify_all(const std::atomic<T>* atomic) {
  static_assert(sizeof(T) == 4, "atomic_notify_all(win32): only 32-bit atomics are supported");
  details::global_wait_stats().notify_all_calls.fetch_add(1, std::memory_order_relaxed);
  if (details::has_native_wait_api()) {
    details::native_wait_api().wake_all(details::wake_address(atomic));
  }
}

template void atomic_wait<uint32_t>(const std::atomic<uint32_t>*, uint32_t);
template void atomic_notify_one<uint32_t>(const std::atomic<uint32_t>*);
template void atomic_notify_all<uint32_t>(const std::atomic<uint32_t>*);

}  // namespace xproc::sync

#endif
