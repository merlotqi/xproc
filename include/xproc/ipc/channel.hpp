#pragma once

#include <cstring>
#include <memory>
#include <stdexcept>
#include <xproc/ipc/endpoint.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/fixed_writer.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/ringbuffer/varlen_writer.hpp>

namespace xproc {
namespace ipc {

class channel : public endpoint {
 public:
  explicit channel(const transport_options& opts, role r) : endpoint(opts, r) {
    if (r != role::producer && r != role::consumer) {
      throw std::logic_error("channel: only producer or consumer roles are supported");
    }
    init_views();
  }

 private:
  void init_views() {
    if (opts_.type == channel_type::fixed) {
      writer_ = std::make_unique<ringbuffer::fixed_writer>(header_);
      reader_ = std::make_unique<ringbuffer::fixed_reader>(header_);
    } else {
      writer_ = std::make_unique<ringbuffer::varlen_writer>(header_);
      reader_ = std::make_unique<ringbuffer::varlen_reader>(header_);
    }
  }

 public:
  template <typename T>
  void send_fixed(const T& data) {
    send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)));
  }

  // Fixed channel: reserve exactly byte_length bytes (same as sizeof(T) for send_fixed<T>).
  void send_fixed_sized(const void* data, std::uint32_t byte_length) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_sized requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_sized requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      throw std::invalid_argument("channel::send_fixed_sized: byte_length exceeds item_size");
    }
    auto* fw = static_cast<ringbuffer::fixed_writer*>(writer_.get());
    std::uint64_t pos = 0;
    void* buf = fw->reserve(byte_length, pos);
    std::memcpy(buf, data, static_cast<std::size_t>(byte_length));
    fw->commit(pos);
  }

  void send_varlen(const void* data, uint32_t len) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_varlen requires producer role");
    }
    auto* vw = static_cast<ringbuffer::varlen_writer*>(writer_.get());
    uint64_t pos;
    void* buf = vw->reserve(len, pos);
    std::memcpy(buf, data, len);
    vw->commit(pos);
  }

  // Fixed channel: payload at most item_size bytes; remainder zero-padded in the slot.
  void send_fixed_bytes(const void* data, std::uint32_t payload_len) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_bytes requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_bytes requires fixed channel");
    }
    if (payload_len > opts_.item_size) {
      throw std::invalid_argument("channel::send_fixed_bytes: payload_len exceeds item_size");
    }
    auto* fw = static_cast<ringbuffer::fixed_writer*>(writer_.get());
    std::uint64_t pos = 0;
    void* buf = fw->reserve(opts_.item_size, pos);
    std::memcpy(buf, data, static_cast<std::size_t>(payload_len));
    if (payload_len < opts_.item_size) {
      std::memset(static_cast<char*>(buf) + payload_len, 0, static_cast<std::size_t>(opts_.item_size - payload_len));
    }
    fw->commit(pos);
  }

  // Handler receives (payload_ptr, length). For fixed channels, length is always opts_.item_size.
  template <typename F>
  bool poll(F&& handler) {
    if (get_role() != role::consumer) {
      throw std::logic_error("channel::poll requires consumer role");
    }
    auto invoke = [&](void* p, std::uint32_t len) { std::forward<F>(handler)(p, len); };
    if (opts_.type == channel_type::fixed) {
      auto* fr = static_cast<ringbuffer::fixed_reader*>(reader_.get());
      const std::uint32_t item = opts_.item_size;
      return fr->read(item, [&](void* p) { invoke(p, item); });
    }
    auto* vr = static_cast<ringbuffer::varlen_reader*>(reader_.get());
    return vr->read(invoke);
  }

 private:
  std::unique_ptr<ringbuffer::ringbuffer_view> writer_;
  std::unique_ptr<ringbuffer::ringbuffer_view> reader_;
};

// Compile-time role split: only send APIs are visible (poll is private via private inheritance).
class producer : private channel {
 public:
  explicit producer(const transport_options& opts) : channel(opts, role::producer) {}

  using channel::get_role;
  using channel::header;
  using channel::is_connected;
  using channel::options;
  using channel::send_fixed;
  using channel::send_fixed_bytes;
  using channel::send_fixed_sized;
  using channel::send_varlen;

  channel& as_channel() noexcept { return static_cast<channel&>(*this); }
  const channel& as_channel() const noexcept { return static_cast<const channel&>(*this); }
};

// Only poll (consume) is public; send APIs stay inaccessible.
class consumer : private channel {
 public:
  explicit consumer(const transport_options& opts) : channel(opts, role::consumer) {}

  using channel::get_role;
  using channel::header;
  using channel::is_connected;
  using channel::options;
  using channel::poll;

  channel& as_channel() noexcept { return static_cast<channel&>(*this); }
  const channel& as_channel() const noexcept { return static_cast<const channel&>(*this); }
};

}  // namespace ipc
}  // namespace xproc
