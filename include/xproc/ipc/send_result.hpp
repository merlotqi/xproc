#pragma once

namespace xproc::ipc {

enum class send_result {
  ok = 0,
  full,
  timeout,
  message_too_large,
  invalid_argument
};

inline const char* send_result_cstr(send_result result) noexcept {
  switch (result) {
    case send_result::ok:
      return "ok";
    case send_result::full:
      return "full";
    case send_result::timeout:
      return "timeout";
    case send_result::message_too_large:
      return "message_too_large";
    case send_result::invalid_argument:
      return "invalid_argument";
    default:
      return "unknown";
  }
}

}  // namespace xproc::ipc
