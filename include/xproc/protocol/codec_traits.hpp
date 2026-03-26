#pragma once

#include <cstddef>
#include <type_traits>

namespace xproc {
namespace protocol {

// Codec contract (C++17; no std::concept):
//
//   using message_type = ...;
//   static constexpr std::size_t max_encoded_size() noexcept;
//   static bool encode(std::byte *dst, std::size_t cap, const message_type &msg, std::size_t &out_len) noexcept;
//   static bool decode(const std::byte *src, std::size_t len, message_type &out) noexcept;
//
// Framing (version ids, checksums, etc.) lives inside encode/decode. The ring carries raw bytes only.

namespace codec_traits_detail {

template <typename Codec, typename = void>
struct has_message_type : std::false_type {};

template <typename Codec>
struct has_message_type<Codec, std::void_t<typename Codec::message_type>> : std::true_type {};

template <typename Codec, typename = void>
struct has_max_encoded_size : std::false_type {};

template <typename Codec>
struct has_max_encoded_size<Codec, std::void_t<std::integral_constant<std::size_t, Codec::max_encoded_size()>>>
    : std::true_type {};

template <typename Codec, typename = void>
struct encode_returns_bool : std::false_type {};

template <typename Codec>
struct encode_returns_bool<Codec, std::void_t<typename Codec::message_type>>
    : std::is_same<decltype(Codec::encode(std::declval<std::byte *>(), std::declval<std::size_t>(),
                                          std::declval<const typename Codec::message_type &>(),
                                          std::declval<std::size_t &>())),
                   bool> {};

template <typename Codec, typename = void>
struct decode_returns_bool : std::false_type {};

template <typename Codec>
struct decode_returns_bool<Codec, std::void_t<typename Codec::message_type>>
    : std::is_same<decltype(Codec::decode(std::declval<const std::byte *>(), std::declval<std::size_t>(),
                                          std::declval<typename Codec::message_type &>())),
                   bool> {};

}  // namespace codec_traits_detail

template <typename Codec>
struct is_codec : std::integral_constant<bool, codec_traits_detail::has_message_type<Codec>::value &&
                                                   codec_traits_detail::has_max_encoded_size<Codec>::value &&
                                                   codec_traits_detail::encode_returns_bool<Codec>::value &&
                                                   codec_traits_detail::decode_returns_bool<Codec>::value> {};

template <typename Codec>
inline constexpr bool is_codec_v = is_codec<Codec>::value;

template <typename Codec, typename = std::enable_if_t<is_codec_v<Codec>>>
using codec_message_type_t = typename Codec::message_type;

template <typename Codec, typename = std::enable_if_t<is_codec_v<Codec>>>
inline constexpr std::size_t codec_max_encoded_size_v = Codec::max_encoded_size();

// Fails compilation with a short message when Codec does not model the contract.
template <typename Codec>
constexpr void assert_codec() noexcept {
  static_assert(is_codec_v<Codec>,
                "Codec must provide message_type, max_encoded_size(), encode, decode per codec_traits.hpp");
}

}  // namespace protocol
}  // namespace xproc
