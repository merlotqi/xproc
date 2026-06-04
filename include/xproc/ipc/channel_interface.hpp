#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <xproc/core/layout_types.hpp>
#include <xproc/ipc/channel.hpp>
#include <xproc/ipc/message_meta.hpp>
#include <xproc/ipc/options.hpp>

namespace xproc::ipc {

// Abstract producer side: fixed / varlen sends (shared memory, TCP, RDMA backends).
class producer_channel_interface {
 public:
  virtual ~producer_channel_interface() = default;

  virtual const transport_options& options() const noexcept = 0;

  /// SHM ring control block; nullptr when not applicable (e.g. socket backend).
  virtual core::control_block* shared_header() noexcept { return nullptr; }
  virtual const core::control_block* shared_header() const noexcept { return nullptr; }

  virtual void send_fixed_bytes(const void* data, std::uint32_t payload_len) {
    send_fixed_bytes(data, payload_len, message_meta{});
  }
  virtual void send_fixed_bytes(const void* data, std::uint32_t payload_len, const message_meta& meta) = 0;

  /// Fixed channel: one ring slot of byte_length bytes (matches channel::send_fixed_sized).
  virtual void send_fixed_sized(const void* data, std::uint32_t byte_length) {
    send_fixed_sized(data, byte_length, message_meta{});
  }
  virtual void send_fixed_sized(const void* data, std::uint32_t byte_length, const message_meta& meta) = 0;

  virtual void send_varlen(const void* data, std::uint32_t len) {
    send_varlen(data, len, message_meta{});
  }
  virtual void send_varlen(const void* data, std::uint32_t len, const message_meta& meta) = 0;

  template <typename T>
  void send_fixed(const T& data) {
    send_fixed(data, message_meta{});
  }

  template <typename T>
  void send_fixed(const T& data, const message_meta& meta) {
    if (options().type != channel_type::fixed) {
      throw std::logic_error("producer_channel_interface::send_fixed requires fixed channel");
    }
    if (sizeof(T) > static_cast<std::size_t>(options().item_size)) {
      throw std::invalid_argument("producer_channel_interface::send_fixed: sizeof(T) exceeds item_size");
    }
    send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)), meta);
  }
};

// Abstract consumer side: poll + idle wait (SHM uses commit_seq wait; socket uses short sleep).
class consumer_channel_interface {
 public:
  virtual ~consumer_channel_interface() = default;

  virtual const transport_options& options() const noexcept = 0;

  virtual core::control_block* shared_header() noexcept { return nullptr; }
  virtual const core::control_block* shared_header() const noexcept { return nullptr; }

  template <typename F>
  bool poll(F&& handler) {
    return poll_impl(
        [&](const message_meta& meta, void* p, std::uint32_t len) { std::forward<F>(handler)(meta, p, len); });
  }

  /// Called when poll returned false: block until new data, an interrupt, or a backend-specific wake condition.
  virtual void wait() = 0;

  /// Interrupts a thread currently blocked in wait(). Backends that do not block in wait() may keep the default.
  virtual void interrupt_wait() noexcept {}

 protected:
  virtual bool poll_impl(const std::function<void(const message_meta&, void*, std::uint32_t)>& handler) = 0;
};

// Wraps existing SHM producer.
class shm_producer final : public producer_channel_interface {
 public:
  explicit shm_producer(const transport_options& opts);
  const transport_options& options() const noexcept override { return ch_.options(); }
  core::control_block* shared_header() noexcept override { return ch_.header(); }
  const core::control_block* shared_header() const noexcept override { return ch_.header(); }
  void send_fixed_bytes(const void* data, std::uint32_t payload_len) override;
  void send_fixed_bytes(const void* data, std::uint32_t payload_len, const message_meta& meta) override;
  void send_fixed_sized(const void* data, std::uint32_t byte_length) override;
  void send_fixed_sized(const void* data, std::uint32_t byte_length, const message_meta& meta) override;
  void send_varlen(const void* data, std::uint32_t len) override;
  void send_varlen(const void* data, std::uint32_t len, const message_meta& meta) override;

  producer& native() noexcept { return ch_; }
  const producer& native() const noexcept { return ch_; }

 private:
  producer ch_;
};

// Wraps existing SHM consumer.
class shm_consumer final : public consumer_channel_interface {
 public:
  explicit shm_consumer(const transport_options& opts);
  const transport_options& options() const noexcept override { return ch_.options(); }
  core::control_block* shared_header() noexcept override { return ch_.header(); }
  const core::control_block* shared_header() const noexcept override { return ch_.header(); }
  void wait() override;

  consumer& native() noexcept { return ch_; }
  const consumer& native() const noexcept { return ch_; }

 protected:
  bool poll_impl(const std::function<void(const message_meta&, void*, std::uint32_t)>& handler) override;

 private:
  consumer ch_;
};

}  // namespace xproc::ipc
