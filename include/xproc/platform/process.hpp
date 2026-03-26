#pragma once

#include <cstdint>
#include <xproc/platform/platform.hpp>

#if defined(XPROC_PLATFORM_LINUX)
#include <unistd.h>
#elif defined(XPROC_PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <processthreadsapi.h>
#endif

namespace xproc {
namespace platform {

inline std::int32_t current_process_id() noexcept {
#if defined(XPROC_PLATFORM_LINUX)
  return static_cast<std::int32_t>(::getpid());
#elif defined(XPROC_PLATFORM_WINDOWS)
  return static_cast<std::int32_t>(::GetCurrentProcessId());
#else
  return 0;
#endif
}

}  // namespace platform
}  // namespace xproc
