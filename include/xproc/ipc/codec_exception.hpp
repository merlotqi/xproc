#pragma once

#include <stdexcept>
#include <string>
#include <system_error>

namespace xproc::ipc {
namespace xproc::ipc {

enum class codec_error {
  encode_failed,
  decode_failed,
  wire_exceeds_item_size,
  wrap_scratch_cap_exceeded,
};

inline const std::error_category& codec_error_category() noexcept {
  class codec_error_category_impl final : public std::error_category {
   public:
    const char* name() const noexcept override { return "xproc.codec"; }
    std::string message(int ev) const override {
      switch (static_cast<codec_error>(ev)) {
        case codec_error::encode_failed:
          return "codec encode failed";
        case codec_error::decode_failed:
          return "codec decode failed";
        case codec_error::wire_exceeds_item_size:
          return "encoded wire payload exceeds fixed item_size";
        case codec_error::wrap_scratch_cap_exceeded:
          return "byte codec wrap failed: scratch cap exceeded";
        default:
          return "unknown codec error";
      }
    }
  };
  static codec_error_category_impl instance;
  return instance;
}

inline std::error_code make_error_code(codec_error e) noexcept { return {static_cast<int>(e), codec_error_category()}; }

// Thrown by send_encoded / poll_decoded / IByteCodec send path on codec or size failures.
class codec_exception : public std::runtime_error {
 public:
  codec_exception(codec_error code, std::string message) : std::runtime_error(std::move(message)), code_(code) {}

  codec_error code() const noexcept { return code_; }
  std::error_code ec() const noexcept { return make_error_code(code_); }

 private:
  codec_error code_;
};

}  // namespace ipc::xproc

namespace std {
template <>
struct is_error_code_enum<xproc::ipc::codec_error> : true_type {};
}  // namespace std
