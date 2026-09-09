#pragma once

#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <xproc/ipc/endpoint.hpp>
#include <xproc/ipc/message_meta.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/send_result.hpp>

#include <spscring/fixed_reader.hpp>
#include <spscring/fixed_writer.hpp>
#include <spscring/varlen_reader.hpp>
#include <spscring/varlen_writer.hpp>

namespace xproc::ipc {

// Both message_meta types have identical layout (24 bytes); this helper converts
// from xproc::ipc::message_meta to spscring::message_meta for API compatibility.
inline spscring::message_meta to_spscring_meta(const message_meta& m) noexcept {
  spscring::message_meta sm;
  sm.user_data = m.user_data;
  sm.timestamp_ns = m.timestamp_ns;
  sm.schema_id = m.schema_id;
  sm.flags = m.flags;
  // Auto-fill timestamp if not set by the caller.
  if (sm.timestamp_ns == 0) {
    sm.timestamp_ns = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  }
  return sm;
}

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
    auto* scb = reinterpret_cast<spscring::control_block*>(header_);
    if (opts_.type == channel_type::fixed) {
      writer_ = std::make_unique<spscring::fixed_writer>(scb);
      reader_ = std::make_unique<spscring::fixed_reader>(scb);
    } else {
      writer_ = std::make_unique<spscring::varlen_writer>(scb);
      reader_ = std::make_unique<spscring::varlen_reader>(scb);
    }
  }

 public:
  std::size_t capacity_bytes() const { return header_ ? static_cast<std::size_t>(header_->spscring_cb.data_capacity) : 0u; }

  std::size_t used_bytes() const {
    if (!header_) {
      return 0u;
    }
    const auto write = header_->spscring_cb.rb_meta.write_pos.load(std::memory_order_acquire);
    const auto read = header_->spscring_cb.rb_meta.read_pos.load(std::memory_order_acquire);
    const auto used = write >= read ? (write - read) : 0;
    const auto cap = static_cast<std::uint64_t>(header_->spscring_cb.data_capacity);
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
    send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)), message_meta{});
  }

  template <typename T>
  void send_fixed(const T& data, const message_meta& meta) {
    send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)), meta);
  }

  // Fixed channel: reserve item_size bytes per slot; zero-pad unused tail.
  void send_fixed_sized(const void* data, std::uint32_t byte_length) {
    send_fixed_sized(data, byte_length, message_meta{});
  }

  void send_fixed_sized(const void* data, std::uint32_t byte_length, const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_sized requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_sized requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      throw std::invalid_argument("channel::send_fixed_sized: byte_length exceeds item_size");
    }
    auto* fw = static_cast<spscring::fixed_writer*>(writer_.get());
    void* buf = fw->spin_until_reserve(backoff_);
    std::memcpy(buf, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(buf) + byte_length, 0, static_cast<std::size_t>(opts_.item_size - byte_length));
    }
    fw->commit();
  }

  void send_varlen(const void* data, uint32_t len) { send_varlen(data, len, message_meta{}); }

  void send_varlen(const void* data, uint32_t len, const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_varlen requires producer role");
    }
    auto* vw = static_cast<spscring::varlen_writer*>(writer_.get());
    auto rr = vw->try_reserve(len, to_spscring_meta(meta));
    if (!rr) {
      throw std::runtime_error("channel::send_varlen: reserve failed");
    }
    std::memcpy(rr.payload, data, len);
    vw->commit(rr.position);
  }

  // Fixed channel: payload at most item_size bytes; remainder zero-padded in the slot.
  void send_fixed_bytes(const void* data, std::uint32_t payload_len) {
    send_fixed_bytes(data, payload_len, message_meta{});
  }

  void send_fixed_bytes(const void* data, std::uint32_t payload_len, const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_bytes requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_bytes requires fixed channel");
    }
    if (payload_len > opts_.item_size) {
      throw std::invalid_argument("channel::send_fixed_bytes: payload_len exceeds item_size");
    }
    auto* fw = static_cast<spscring::fixed_writer*>(writer_.get());
    void* buf = fw->spin_until_reserve(backoff_);
    std::memcpy(buf, data, static_cast<std::size_t>(payload_len));
    if (payload_len < opts_.item_size) {
      std::memset(static_cast<char*>(buf) + payload_len, 0, static_cast<std::size_t>(opts_.item_size - payload_len));
    }
    fw->commit();
  }

  // ---- non-blocking and bounded-time fixed send ----

  send_result try_send_fixed_sized(const void* data, std::uint32_t byte_length) {
    return try_send_fixed_sized(data, byte_length, message_meta{});
  }

  send_result try_send_fixed_sized(const void* data, std::uint32_t byte_length, const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::try_send_fixed_sized requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::try_send_fixed_sized requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      return send_result::invalid_argument;
    }
    auto* fw = static_cast<spscring::fixed_writer*>(writer_.get());
    void* buf = fw->try_reserve();
    if (!buf) {
      return send_result::full;
    }
    std::memcpy(buf, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(buf) + byte_length, 0,
                  static_cast<std::size_t>(opts_.item_size - byte_length));
    }
    fw->commit();
    return send_result::ok;
  }

  template <typename T>
  bool try_send_fixed(const T& data) {
    return try_send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)), message_meta{}) == send_result::ok;
  }

  template <typename T>
  bool try_send_fixed(const T& data, const message_meta& meta) {
    return try_send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)), meta) == send_result::ok;
  }

  template <typename Rep, typename Period>
  send_result send_fixed_sized_for(const void* data, std::uint32_t byte_length,
                                   const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(data, byte_length, timeout, message_meta{});
  }

  template <typename Rep, typename Period>
  send_result send_fixed_sized_for(const void* data, std::uint32_t byte_length,
                                   const std::chrono::duration<Rep, Period>& timeout, const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_sized_for requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_sized_for requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      return send_result::invalid_argument;
    }
    auto* fw = static_cast<spscring::fixed_writer*>(writer_.get());
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      void* buf = fw->try_reserve();
      if (buf) {
        std::memcpy(buf, data, static_cast<std::size_t>(byte_length));
        if (byte_length < opts_.item_size) {
          std::memset(static_cast<char*>(buf) + byte_length, 0,
                      static_cast<std::size_t>(opts_.item_size - byte_length));
        }
        fw->commit();
        return send_result::ok;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return send_result::timeout;
      }
      std::this_thread::yield();
    }
  }

  template <typename T, typename Rep, typename Period>
  send_result send_fixed_for(const T& data, const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(&data, static_cast<std::uint32_t>(sizeof(T)), timeout, message_meta{});
  }

  template <typename T, typename Rep, typename Period>
  send_result send_fixed_for(const T& data, const std::chrono::duration<Rep, Period>& timeout,
                             const message_meta& meta) {
    return send_fixed_sized_for(&data, static_cast<std::uint32_t>(sizeof(T)), timeout, meta);
  }

  send_result try_send_fixed_bytes(const void* data, std::uint32_t payload_len) {
    return try_send_fixed_sized(data, payload_len, message_meta{});
  }

  send_result try_send_fixed_bytes(const void* data, std::uint32_t payload_len, const message_meta& meta) {
    return try_send_fixed_sized(data, payload_len, meta);
  }

  template <typename Rep, typename Period>
  send_result send_fixed_bytes_for(const void* data, std::uint32_t payload_len,
                                   const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(data, payload_len, timeout, message_meta{});
  }

  template <typename Rep, typename Period>
  send_result send_fixed_bytes_for(const void* data, std::uint32_t payload_len,
                                   const std::chrono::duration<Rep, Period>& timeout, const message_meta& meta) {
    return send_fixed_sized_for(data, payload_len, timeout, meta);
  }

  // ---- non-blocking and bounded-time varlen send ----

  send_result try_send_varlen(const void* data, std::uint32_t len) {
    return try_send_varlen(data, len, message_meta{});
  }

  send_result try_send_varlen(const void* data, std::uint32_t len, const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::try_send_varlen requires producer role");
    }
    if (opts_.type != channel_type::varlen) {
      throw std::logic_error("channel::try_send_varlen requires variable channel");
    }
    auto* vw = static_cast<spscring::varlen_writer*>(writer_.get());
    auto rr = vw->try_reserve(len, to_spscring_meta(meta));
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, len);
    vw->commit(rr.position);
    return send_result::ok;
  }

  template <typename Rep, typename Period>
  send_result send_varlen_for(const void* data, std::uint32_t len, const std::chrono::duration<Rep, Period>& timeout) {
    return send_varlen_for(data, len, timeout, message_meta{});
  }

  template <typename Rep, typename Period>
  send_result send_varlen_for(const void* data, std::uint32_t len, const std::chrono::duration<Rep, Period>& timeout,
                              const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_varlen_for requires producer role");
    }
    if (opts_.type != channel_type::varlen) {
      throw std::logic_error("channel::send_varlen_for requires variable channel");
    }
    auto* vw = static_cast<spscring::varlen_writer*>(writer_.get());
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      auto rr = vw->try_reserve(len, to_spscring_meta(meta));
      if (rr) {
        std::memcpy(rr.payload, data, len);
        vw->commit(rr.position);
        return send_result::ok;
      }
      if (rr.status != spscring::reserve_status::full) {
        return map_reserve_status(rr.status);
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return send_result::timeout;
      }
      std::this_thread::yield();
    }
  }

  // Handler receives (meta, payload_ptr, length). For fixed channels, length is always opts_.item_size.
  template <typename F>
  bool poll(F&& handler) {
    if (get_role() != role::consumer) {
      throw std::logic_error("channel::poll requires consumer role");
    }
    if (opts_.type == channel_type::fixed) {
      auto* fr = static_cast<spscring::fixed_reader*>(reader_.get());
      const void* slot = fr->try_read();
      if (!slot) {
        return false;
      }
      // fixed_reader doesn't deliver meta; construct a default.
      handler(message_meta{}, const_cast<void*>(slot), opts_.item_size);
      fr->read_advance();
      return true;
    }
    auto* vr = static_cast<spscring::varlen_reader*>(reader_.get());
    // Adapt spscring handler signature: (uint8_t*&, uint32_t&, message_meta&, uint64_t&)
    // to xproc handler signature: (const message_meta&, void*, uint32_t)
    return vr->read([&](std::uint8_t*& payload, std::uint32_t& size, spscring::message_meta& smeta,
                        std::uint64_t& /*reserved*/) {
      message_meta mmeta;
      mmeta.user_data = smeta.user_data;
      mmeta.timestamp_ns = smeta.timestamp_ns;
      mmeta.schema_id = smeta.schema_id;
      mmeta.flags = smeta.flags;
      std::forward<F>(handler)(mmeta, static_cast<void*>(payload), size);
      return true;
    });
  }

 private:
  static send_result map_reserve_status(spscring::reserve_status status) noexcept {
    switch (status) {
      case spscring::reserve_status::ok:
        return send_result::ok;
      case spscring::reserve_status::full:
        return send_result::full;
      case spscring::reserve_status::timeout:
        return send_result::timeout;
      case spscring::reserve_status::message_too_large:
        return send_result::message_too_large;
      default:
        return send_result::invalid_argument;
    }
  }

  std::unique_ptr<spscring::ring_view> writer_;
  std::unique_ptr<spscring::ring_view> reader_;
  spscring::atomic_backoff backoff_;
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
