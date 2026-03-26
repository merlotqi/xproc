#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
#include <xproc/ipc/ipc_channel.hpp>
#include <xproc/protocol/codec_traits.hpp>
#include <xproc/protocol/protocol.hpp>

namespace xproc {
namespace ipc {

namespace detail {

inline constexpr std::size_t send_encoded_stack_buf_max_v = 4096;

template <typename Codec>
void send_encoded_dispatch(ipc_channel &ch, const typename Codec::message_type &msg) {
  constexpr std::size_t cap = Codec::max_encoded_size();
  std::size_t out_len = 0;
  if constexpr (cap <= send_encoded_stack_buf_max_v) {
    std::array<std::byte, cap> buf{};
    if (!Codec::encode(buf.data(), buf.size(), msg, out_len)) {
      throw std::runtime_error("send_encoded: encode failed");
    }
    if (ch.options().type == channel_type::variable) {
      ch.send_varlen(buf.data(), static_cast<std::uint32_t>(out_len));
    } else {
      if (out_len > static_cast<std::size_t>(ch.options().item_size)) {
        throw std::runtime_error("send_encoded: encoded size exceeds item_size for fixed channel");
      }
      ch.send_fixed_bytes(buf.data(), static_cast<std::uint32_t>(out_len));
    }
  } else {
    std::vector<std::byte> buf(cap);
    if (!Codec::encode(buf.data(), buf.size(), msg, out_len)) {
      throw std::runtime_error("send_encoded: encode failed");
    }
    if (ch.options().type == channel_type::variable) {
      ch.send_varlen(buf.data(), static_cast<std::uint32_t>(out_len));
    } else {
      if (out_len > static_cast<std::size_t>(ch.options().item_size)) {
        throw std::runtime_error("send_encoded: encoded size exceeds item_size for fixed channel");
      }
      ch.send_fixed_bytes(buf.data(), static_cast<std::uint32_t>(out_len));
    }
  }
}

}  // namespace detail

template <typename Codec>
void send_encoded(ipc_channel &ch, const typename Codec::message_type &msg) {
  static_assert(xproc::protocol::is_codec_v<Codec>,
                "send_encoded requires a type satisfying xproc::protocol::is_codec");
  detail::send_encoded_dispatch<Codec>(ch, msg);
}

template <typename Codec>
void send_encoded(producer_channel &ch, const typename Codec::message_type &msg) {
  send_encoded<Codec>(ch.as_ipc_channel(), msg);
}

// Decodes into message_type and passes to handler. If decode uses a view into ring memory
// (e.g. span_codec / string_view), copy out inside handler before returning if you use msg async.
template <typename Codec, typename F>
bool poll_decoded(ipc_channel &ch, F &&handler) {
  static_assert(xproc::protocol::is_codec_v<Codec>,
                "poll_decoded requires a type satisfying xproc::protocol::is_codec");
  typename Codec::message_type msg{};
  return ch.poll([&](void *p, std::uint32_t len) {
    if (!Codec::decode(static_cast<const std::byte *>(p), static_cast<std::size_t>(len), msg)) {
      throw std::runtime_error("poll_decoded: decode failed");
    }
    std::forward<F>(handler)(msg);
  });
}

template <typename Codec, typename F>
bool poll_decoded(consumer_channel &ch, F &&handler) {
  return poll_decoded<Codec>(ch.as_ipc_channel(), std::forward<F>(handler));
}

// Dynamic codec: grows scratch until wrap succeeds (identity needs wire_cap >= logical_len).
inline void send_encoded(ipc_channel &ch, const protocol::IByteCodec &codec, const std::byte *logical,
                         std::size_t logical_len, std::vector<std::byte> &scratch) {
  std::size_t cap = logical_len < 64 ? 64 : logical_len;
  while (true) {
    scratch.resize(cap);
    std::size_t wire_len = 0;
    if (codec.wrap(logical, logical_len, scratch.data(), scratch.size(), wire_len)) {
      if (ch.options().type == channel_type::variable) {
        ch.send_varlen(scratch.data(), static_cast<std::uint32_t>(wire_len));
      } else {
        if (wire_len > static_cast<std::size_t>(ch.options().item_size)) {
          throw std::runtime_error("send_encoded(IByteCodec): wire length exceeds item_size");
        }
        ch.send_fixed_bytes(scratch.data(), static_cast<std::uint32_t>(wire_len));
      }
      return;
    }
    cap *= 2;
    if (cap > 16 * 1024 * 1024) {
      throw std::runtime_error("send_encoded(IByteCodec): wrap failed (scratch cap)");
    }
  }
}

inline void send_encoded(producer_channel &ch, const protocol::IByteCodec &codec, const std::byte *logical,
                         std::size_t logical_len, std::vector<std::byte> &scratch) {
  send_encoded(ch.as_ipc_channel(), codec, logical, logical_len, scratch);
}

}  // namespace ipc
}  // namespace xproc
