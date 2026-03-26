#pragma once

// Reference JSON Codec: enable with CMake option XPROC_WITH_NLOHMANN_JSON (find_package nlohmann_json).

#ifdef XPROC_WITH_NLOHMANN_JSON

#include <cstddef>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>

namespace xproc {
namespace protocol {

// UTF-8 JSON text on the wire (no extra framing; ring varlen/fixed carries the byte length).
template <std::size_t MaxWire>
struct nlohmann_json_codec {
  using message_type = nlohmann::json;

  static constexpr std::size_t max_encoded_size() noexcept { return MaxWire; }

  static bool encode(std::byte *dst, std::size_t cap, const nlohmann::json &msg, std::size_t &out_len) noexcept {
    try {
      const std::string s = msg.dump();
      if (s.size() > MaxWire || s.size() > cap) {
        return false;
      }
      std::memcpy(dst, s.data(), s.size());
      out_len = s.size();
      return true;
    } catch (...) {
      return false;
    }
  }

  static bool decode(const std::byte *src, std::size_t len, nlohmann::json &out) noexcept {
    try {
      out = nlohmann::json::parse(reinterpret_cast<const char *>(src), reinterpret_cast<const char *>(src) + len);
      return true;
    } catch (...) {
      return false;
    }
  }
};

}  // namespace protocol
}  // namespace xproc

#endif  // XPROC_WITH_NLOHMANN_JSON
