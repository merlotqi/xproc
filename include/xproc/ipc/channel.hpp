#pragma once

#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <xproc/ipc/endpoint.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/send_result.hpp>
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/fixed_writer.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/ringbuffer/varlen_writer.hpp>

namespace xproc::ipc {

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
  std::size_t capacity_bytes() const { return header_ ? static_cast<std::size_t>(header_->data_capacity) : 0u; }

  std::size_t used_bytes() const {
    if (!header_) {
      return 0u;
    }
    const auto write = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    const auto read = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    const auto used = write >= read ? (write - read) : 0;
    const auto cap = static_cast<std::uint64_t>(header_->data_capacity);
    return static_cast<std::size_t>(used > cap ? cap : used);
  }

  std::size_t available_bytes() const { return capacity_bytes() - used_bytes(); }

  double fill_ratio() const {
    const auto cap = capacity_bytes();
    if (cap == 0) {
      return 0.0;
    }
    return static_cast<double>(used_bytes()) / static_cast<double>(cap);
  }

  template <typename T>
  void send_fixed(const T& data) {
    send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)));
  }

  // Fixed channel: reserve item_size bytes per slot; zero-pad unused tail.
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
    void* buf = fw->reserve(opts_.item_size, pos);
    std::memcpy(buf, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(buf) + byte_length, 0, static_cast<std::size_t>(opts_.item_size - byte_length));
    }
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

  // ---- non-blocking and bounded-time fixed send ----

  send_result try_send_fixed_sized(const void* data, std::uint32_t byte_length) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::try_send_fixed_sized requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::try_send_fixed_sized requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      return send_result::invalid_argument;
    }
    auto* fw = static_cast<ringbuffer::fixed_writer*>(writer_.get());
    auto rr = fw->try_reserve(opts_.item_size);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(rr.payload) + byte_length, 0,
                  static_cast<std::size_t>(opts_.item_size - byte_length));
    }
    fw->commit(rr.position);
    return send_result::ok;
  }

  template <typename T>
  bool try_send_fixed(const T& data) {
    return try_send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T))) == send_result::ok;
  }

  template <typename Rep, typename Period>
  send_result send_fixed_sized_for(const void* data, std::uint32_t byte_length,
                                   const std::chrono::duration<Rep, Period>& timeout) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_sized_for requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_sized_for requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      return send_result::invalid_argument;
    }
    auto* fw = static_cast<ringbuffer::fixed_writer*>(writer_.get());
    auto rr = fw->reserve_for(opts_.item_size, timeout);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(rr.payload) + byte_length, 0,
                  static_cast<std::size_t>(opts_.item_size - byte_length));
    }
    fw->commit(rr.position);
    return send_result::ok;
  }

  template <typename T, typename Rep, typename Period>
  send_result send_fixed_for(const T& data, const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(&data, static_cast<std::uint32_t>(sizeof(T)), timeout);
  }

  send_result try_send_fixed_bytes(const void* data, std::uint32_t payload_len) {
    return try_send_fixed_sized(data, payload_len);
  }

  template <typename Rep, typename Period>
  send_result send_fixed_bytes_for(const void* data, std::uint32_t payload_len,
                                   const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(data, payload_len, timeout);
  }

  // ---- non-blocking and bounded-time varlen send ----

  send_result try_send_varlen(const void* data, std::uint32_t len) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::try_send_varlen requires producer role");
    }
    if (opts_.type != channel_type::varlen) {
      throw std::logic_error("channel::try_send_varlen requires variable channel");
    }
    auto* vw = static_cast<ringbuffer::varlen_writer*>(writer_.get());
    auto rr = vw->try_reserve(len);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, len);
    vw->commit(rr.position);
    return send_result::ok;
  }

  template <typename Rep, typename Period>
  send_result send_varlen_for(const void* data, std::uint32_t len, const std::chrono::duration<Rep, Period>& timeout) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_varlen_for requires producer role");
    }
    if (opts_.type != channel_type::varlen) {
      throw std::logic_error("channel::send_varlen_for requires variable channel");
    }
    auto* vw = static_cast<ringbuffer::varlen_writer*>(writer_.get());
    auto rr = vw->reserve_for(len, timeout);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, len);
    vw->commit(rr.position);
    return send_result::ok;
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
  static send_result map_reserve_status(ringbuffer::reserve_status status) noexcept {
    switch (status) {
      case ringbuffer::reserve_status::ok:
        return send_result::ok;
      case ringbuffer::reserve_status::full:
        return send_result::full;
      case ringbuffer::reserve_status::timeout:
        return send_result::timeout;
      case ringbuffer::reserve_status::message_too_large:
        return send_result::message_too_large;
      default:
        return send_result::invalid_argument;
    }
  }

  std::unique_ptr<ringbuffer::ringbuffer_view> writer_;
  std::unique_ptr<ringbuffer::ringbuffer_view> reader_;
};

// Compile-time role split: only send APIs are visible (poll is private via private inheritance).
class producer : private channel {
 public:
  explicit producer(const transport_options& opts) : channel(opts, role::producer) {}

  using channel::available_bytes;
  using channel::capacity_bytes;
  using channel::fill_ratio;
  using channel::get_role;
  using channel::header;
  using channel::is_connected;
  using channel::options;
  using channel::send_fixed;
  using channel::send_fixed_bytes;
  using channel::send_fixed_bytes_for;
  using channel::send_fixed_for;
  using channel::send_fixed_sized;
  using channel::send_fixed_sized_for;
  using channel::send_varlen;
  using channel::send_varlen_for;
  using channel::try_send_fixed;
  using channel::try_send_fixed_bytes;
  using channel::try_send_fixed_sized;
  using channel::try_send_varlen;
  using channel::used_bytes;

  channel& as_channel() noexcept { return static_cast<channel&>(*this); }
  const channel& as_channel() const noexcept { return static_cast<const channel&>(*this); }
};

// Only poll (consume) is public; send APIs stay inaccessible.
class consumer : private channel {
 public:
  explicit consumer(const transport_options& opts) : channel(opts, role::consumer) {}

  using channel::available_bytes;
  using channel::capacity_bytes;
  using channel::fill_ratio;
  using channel::get_role;
  using channel::header;
  using channel::is_connected;
  using channel::options;
  using channel::poll;
  using channel::used_bytes;

  channel& as_channel() noexcept { return static_cast<channel&>(*this); }
  const channel& as_channel() const noexcept { return static_cast<const channel&>(*this); }
};

}  // namespace xproc::ipc
