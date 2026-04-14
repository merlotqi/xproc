#pragma once

namespace xproc::ringbuffer {

// Lightweight status for tests and upper layers; hot-path writers/readers may ignore it.
enum class ringbuffer_error {
  ok = 0,
  empty,
  full,
  incomplete,  // e.g. slot reserved but not yet committed (status != published)
  invalid_argument
};

inline const char* ringbuffer_error_cstr(ringbuffer_error e) noexcept {
  switch (e) {
    case ringbuffer_error::ok:
      return "ok";
    case ringbuffer_error::empty:
      return "empty";
    case ringbuffer_error::full:
      return "full";
    case ringbuffer_error::incomplete:
      return "incomplete";
    case ringbuffer_error::invalid_argument:
      return "invalid_argument";
    default:
      return "unknown";
  }
}

}  // namespace xproc::ringbuffer
