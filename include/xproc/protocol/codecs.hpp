#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <xproc/protocol/codec_traits.hpp>

namespace xproc::protocol {
namespace xproc::protocol {

template <typename T>
struct raw_pod_codec {
  using message_type = T;

  static constexpr std::size_t max_encoded_size() noexcept { return sizeof(T); }

  static bool encode(std::byte* dst, std::size_t cap, const T& msg, std::size_t& out_len) noexcept {
    static_assert(std::is_trivially_copyable<T>::value, "raw_pod_codec requires trivially copyable T");
    if (cap < sizeof(T)) {
      return false;
    }
    std::memcpy(dst, &msg, sizeof(T));
    out_len = sizeof(T);
    return true;
  }

  static bool decode(const std::byte* src, std::size_t len, T& out) noexcept {
    if (len < sizeof(T)) {
      return false;
    }
    std::memcpy(&out, src, sizeof(T));
    return true;
  }
};

// Variable-length payload up to MaxPayload bytes (length prefix not on wire; ring varlen header carries length).
template <std::size_t MaxPayload>
struct bounded_bytes_codec {
  struct message_type {
    std::uint32_t size{0};
    std::array<std::byte, MaxPayload> bytes{};
  };

  static constexpr std::size_t max_encoded_size() noexcept { return MaxPayload; }

  static bool encode(std::byte* dst, std::size_t cap, const message_type& msg, std::size_t& out_len) noexcept {
    if (msg.size > MaxPayload || static_cast<std::size_t>(msg.size) > cap) {
      return false;
    }
    std::memcpy(dst, msg.bytes.data(), static_cast<std::size_t>(msg.size));
    out_len = static_cast<std::size_t>(msg.size);
    return true;
  }

  static bool decode(const std::byte* src, std::size_t len, message_type& out) noexcept {
    if (len > MaxPayload) {
      return false;
    }
    out.size = static_cast<std::uint32_t>(len);
    std::memcpy(out.bytes.data(), src, static_cast<std::size_t>(len));
    return true;
  }
};

// Non-owning wire view (C++17: std::basic_string_view<std::byte>; complements bounded_bytes_codec).
// decode() sets the view to ring bytes; valid only until poll_decoded's handler returns — copy if needed async.
template <std::size_t MaxN>
struct span_codec {
  using message_type = std::basic_string_view<std::byte>;

  static constexpr std::size_t max_encoded_size() noexcept { return MaxN; }

  static bool encode(std::byte* dst, std::size_t cap, message_type msg, std::size_t& out_len) noexcept {
    if (msg.size() > MaxN || msg.size() > cap) {
      return false;
    }
    std::memcpy(dst, msg.data(), msg.size());
    out_len = msg.size();
    return true;
  }

  static bool decode(const std::byte* src, std::size_t len, message_type& out) noexcept {
    if (len > MaxN) {
      return false;
    }
    out = message_type(src, len);
    return true;
  }
};

}  // namespace protocol::xproc
