#pragma once

// Reference Protobuf Codec: enable with CMake option XPROC_WITH_PROTOBUF (find_package Protobuf).

#ifdef XPROC_WITH_PROTOBUF

#include <google/protobuf/message.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace xproc::protocol {

// Wire format is Message::SerializeToArray bytes (no extra length prefix; ring supplies length).
template <typename MessageType, std::size_t MaxWire>
struct protobuf_message_codec {
  static_assert(std::is_base_of<google::protobuf::Message, MessageType>::value,
                "protobuf_message_codec requires a google::protobuf::Message subclass");

  using message_type = MessageType;

  static constexpr std::size_t max_encoded_size() noexcept { return MaxWire; }

  static bool encode(std::byte* dst, std::size_t cap, const MessageType& msg, std::size_t& out_len) noexcept {
    const std::uint64_t need64 = msg.ByteSizeLong();
    if (need64 > static_cast<std::uint64_t>(MaxWire) || need64 > cap ||
        need64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    const int need = static_cast<int>(need64);
    if (!msg.SerializeToArray(dst, need)) {
      return false;
    }
    out_len = static_cast<std::size_t>(need);
    return true;
  }

  static bool decode(const std::byte* src, std::size_t len, MessageType& out) noexcept {
    if (len > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    return out.ParseFromArray(src, static_cast<int>(len));
  }
};

}  // namespace protocol::xproc

#endif  // XPROC_WITH_PROTOBUF
