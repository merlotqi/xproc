#pragma once

#include <stdexcept>
#include <string>
#include <system_error>

namespace xproc {
namespace ipc {

enum class codec_error {
  encode_failed,
  decode_failed,
  wire_exceeds_item_size,
  wrap_scratch_cap_exceeded,
};

// Thrown by send_encoded / poll_decoded / IByteCodec send path on codec or size failures.
class codec_exception : public std::runtime_error {
 public:
  codec_exception(codec_error code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}

  codec_error code() const noexcept { return code_; }

 private:
  codec_error code_;
};

}  // namespace ipc
}  // namespace xproc
