#pragma once

#include <cstddef>
#include <cstring>

namespace xproc::protocol {

// Runtime-polymorphic byte framing (optional). Prefer template codecs in codecs.hpp + ipc_messaging.hpp
// for zero overhead and custom serialization without virtual calls.
class IByteCodec {
 public:
  virtual ~IByteCodec() = default;

  // Pack logical payload into wire bytes (e.g. add headers). wire_len <= wire_cap on success.
  virtual bool wrap(const std::byte* logical, std::size_t logical_len, std::byte* wire, std::size_t wire_cap,
                    std::size_t& wire_len) const = 0;

  // Unpack wire bytes into logical payload (e.g. strip headers). logical_len <= logical_cap on success.
  virtual bool unwrap(const std::byte* wire, std::size_t wire_len, std::byte* logical, std::size_t logical_cap,
                      std::size_t& logical_len) const = 0;
};

// Passthrough: wire == logical (typical for varlen IPC when framing is inside logical bytes).
class identity_byte_codec final : public IByteCodec {
 public:
  bool wrap(const std::byte* logical, std::size_t logical_len, std::byte* wire, std::size_t wire_cap,
            std::size_t& wire_len) const override {
    if (logical_len > wire_cap) {
      return false;
    }
    std::memcpy(wire, logical, logical_len);
    wire_len = logical_len;
    return true;
  }

  bool unwrap(const std::byte* wire, std::size_t wire_len, std::byte* logical, std::size_t logical_cap,
              std::size_t& logical_len) const override {
    if (wire_len > logical_cap) {
      return false;
    }
    std::memcpy(logical, wire, wire_len);
    logical_len = wire_len;
    return true;
  }
};

}  // namespace xproc::protocol
