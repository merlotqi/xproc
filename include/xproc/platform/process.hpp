#pragma once

#include <cstdint>
#include <xproc/platform/platform.hpp>

#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
#include <unistd.h>
#elif defined(XPROC_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Include windows.h before subsystem headers so winnt.h sees MSVC target-architecture macros.
#include <windows.h>
#endif

namespace xproc::platform {

inline std::int32_t current_process_id() noexcept {
#if defined(XPROC_PLATFORM_LINUX) || defined(XPROC_PLATFORM_DARWIN)
  return static_cast<std::int32_t>(::getpid());
#elif defined(XPROC_PLATFORM_WINDOWS)
  return static_cast<std::int32_t>(::GetCurrentProcessId());
#else
  return 0;
#endif
}

}  // namespace platform::xproc
