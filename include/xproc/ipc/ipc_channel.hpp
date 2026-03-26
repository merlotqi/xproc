#pragma once

#include <cstring>
#include <memory>
#include <stdexcept>
#include <xproc/ipc/ipc_endpoint.hpp>
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/fixed_writer.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/ringbuffer/varlen_writer.hpp>

#include <xproc/ipc/ipc_options.hpp>

namespace xproc {
namespace ipc {

class ipc_channel : public ipc_endpoint {
 public:
  explicit ipc_channel(const transport_options &opts, role r) : ipc_endpoint(opts, r) {
    if (r != role::producer && r != role::consumer) {
      throw std::logic_error("ipc_channel: only producer or consumer roles are supported");
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
  void send_fixed(const T &data) {
    if (user_role() != role::producer) {
      throw std::logic_error("ipc_channel::send_fixed requires producer role");
    }
    auto *fw = static_cast<ringbuffer::fixed_writer *>(writer_.get());
    uint64_t pos;
    void *buf = fw->reserve(sizeof(T), pos);
    std::memcpy(buf, &data, sizeof(T));
    fw->commit(pos);
  }

  void send_varlen(const void *data, uint32_t len) {
    if (user_role() != role::producer) {
      throw std::logic_error("ipc_channel::send_varlen requires producer role");
    }
    auto *vw = static_cast<ringbuffer::varlen_writer *>(writer_.get());
    uint64_t pos;
    void *buf = vw->reserve(len, pos);
    std::memcpy(buf, data, len);
    vw->commit(pos);
  }

  // Fixed channel: payload at most item_size bytes; remainder zero-padded in the slot.
  void send_fixed_bytes(const void *data, std::uint32_t payload_len) {
    if (user_role() != role::producer) {
      throw std::logic_error("ipc_channel::send_fixed_bytes requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("ipc_channel::send_fixed_bytes requires fixed channel");
    }
    if (payload_len > opts_.item_size) {
      throw std::invalid_argument("ipc_channel::send_fixed_bytes: payload_len exceeds item_size");
    }
    auto *fw = static_cast<ringbuffer::fixed_writer *>(writer_.get());
    std::uint64_t pos = 0;
    void *buf = fw->reserve(opts_.item_size, pos);
    std::memcpy(buf, data, static_cast<std::size_t>(payload_len));
    if (payload_len < opts_.item_size) {
      std::memset(static_cast<char *>(buf) + payload_len, 0, static_cast<std::size_t>(opts_.item_size - payload_len));
    }
    fw->commit(pos);
  }

  // Handler receives (payload_ptr, length). For fixed channels, length is always opts_.item_size.
  template <typename F>
  bool poll(F &&handler) {
    if (user_role() != role::consumer) {
      throw std::logic_error("ipc_channel::poll requires consumer role");
    }
    auto invoke = [&](void *p, std::uint32_t len) { std::forward<F>(handler)(p, len); };
    if (opts_.type == channel_type::fixed) {
      auto *fr = static_cast<ringbuffer::fixed_reader *>(reader_.get());
      const std::uint32_t item = opts_.item_size;
      return fr->try_read(item, [&](void *p) { invoke(p, item); });
    }
    auto *vr = static_cast<ringbuffer::varlen_reader *>(reader_.get());
    return vr->try_read(invoke);
  }

 private:
  std::unique_ptr<ringbuffer::ringbuffer_view> writer_;
  std::unique_ptr<ringbuffer::ringbuffer_view> reader_;
};

// Compile-time role split: only send APIs are visible (poll is private via private inheritance).
class producer_channel : private ipc_channel {
 public:
  explicit producer_channel(const transport_options &opts) : ipc_channel(opts, role::producer) {}

  using ipc_channel::header;
  using ipc_channel::is_connect;
  using ipc_channel::options;
  using ipc_channel::send_fixed;
  using ipc_channel::send_fixed_bytes;
  using ipc_channel::send_varlen;
  using ipc_channel::user_role;

  ipc_channel &as_ipc_channel() noexcept { return static_cast<ipc_channel &>(*this); }
  const ipc_channel &as_ipc_channel() const noexcept { return static_cast<const ipc_channel &>(*this); }
};

// Only poll (consume) is public; send APIs stay inaccessible.
class consumer_channel : private ipc_channel {
 public:
  explicit consumer_channel(const transport_options &opts) : ipc_channel(opts, role::consumer) {}

  using ipc_channel::header;
  using ipc_channel::is_connect;
  using ipc_channel::options;
  using ipc_channel::poll;
  using ipc_channel::user_role;

  ipc_channel &as_ipc_channel() noexcept { return static_cast<ipc_channel &>(*this); }
  const ipc_channel &as_ipc_channel() const noexcept { return static_cast<const ipc_channel &>(*this); }
};

}  // namespace ipc
}  // namespace xproc
