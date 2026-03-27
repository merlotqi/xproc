#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <xproc/ipc/ipc_channel.hpp>
#include <xproc/ipc/ipc_options.hpp>
#include <xproc/shm/shm_layout.hpp>

namespace xproc {
namespace ipc {

// Abstract producer side: fixed / varlen sends (shared memory, TCP, RDMA backends).
class IProducerChannel {
 public:
  virtual ~IProducerChannel() = default;

  virtual const transport_options& options() const noexcept = 0;

  /// SHM ring control block; nullptr when not applicable (e.g. socket backend).
  virtual shm::shm_control_block* shared_header() noexcept { return nullptr; }
  virtual const shm::shm_control_block* shared_header() const noexcept { return nullptr; }

  virtual void send_fixed_bytes(const void* data, std::uint32_t payload_len) = 0;
  /// Fixed channel: one ring slot of byte_length bytes (matches ipc_channel::send_fixed_sized).
  virtual void send_fixed_sized(const void* data, std::uint32_t byte_length) = 0;
  virtual void send_varlen(const void* data, std::uint32_t len) = 0;

  template <typename T>
  void send_fixed(const T& data) {
    if (options().type != channel_type::fixed) {
      throw std::logic_error("IProducerChannel::send_fixed requires fixed channel");
    }
    if (sizeof(T) > static_cast<std::size_t>(options().item_size)) {
      throw std::invalid_argument("IProducerChannel::send_fixed: sizeof(T) exceeds item_size");
    }
    send_fixed_sized(&data, static_cast<std::uint32_t>(sizeof(T)));
  }
};

// Abstract consumer side: poll + idle wait (SHM uses commit_seq wait; socket uses short sleep).
class IConsumerChannel {
 public:
  virtual ~IConsumerChannel() = default;

  virtual const transport_options& options() const noexcept = 0;

  virtual shm::shm_control_block* shared_header() noexcept { return nullptr; }
  virtual const shm::shm_control_block* shared_header() const noexcept { return nullptr; }

  template <typename F>
  bool poll(F&& handler) {
    return poll_impl([&](void* p, std::uint32_t len) { std::forward<F>(handler)(p, len); });
  }

  /// Called when poll returned false: block until new data or return after a short sleep (socket).
  virtual void wait_when_empty() = 0;

 protected:
  virtual bool poll_impl(const std::function<void(void*, std::uint32_t)>& handler) = 0;
};

// Wraps existing SHM producer_channel.
class shm_producer_transport final : public IProducerChannel {
 public:
  explicit shm_producer_transport(const transport_options& opts);
  const transport_options& options() const noexcept override { return ch_.options(); }
  shm::shm_control_block* shared_header() noexcept override { return ch_.header(); }
  const shm::shm_control_block* shared_header() const noexcept override { return ch_.header(); }
  void send_fixed_bytes(const void* data, std::uint32_t payload_len) override;
  void send_fixed_sized(const void* data, std::uint32_t byte_length) override;
  void send_varlen(const void* data, std::uint32_t len) override;

  producer_channel& native() noexcept { return ch_; }
  const producer_channel& native() const noexcept { return ch_; }

 private:
  producer_channel ch_;
};

// Wraps existing SHM consumer_channel.
class shm_consumer_transport final : public IConsumerChannel {
 public:
  explicit shm_consumer_transport(const transport_options& opts);
  const transport_options& options() const noexcept override { return ch_.options(); }
  shm::shm_control_block* shared_header() noexcept override { return ch_.header(); }
  const shm::shm_control_block* shared_header() const noexcept override { return ch_.header(); }
  void wait_when_empty() override;

  consumer_channel& native() noexcept { return ch_; }
  const consumer_channel& native() const noexcept { return ch_; }

 protected:
  bool poll_impl(const std::function<void(void*, std::uint32_t)>& handler) override;

 private:
  consumer_channel ch_;
};

}  // namespace ipc
}  // namespace xproc
