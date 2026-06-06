#pragma once

#include <cstdint>
#include <xproc/ipc/channel_interface.hpp>
#include <xproc/ipc/message_meta.hpp>

namespace xproc::ipc {

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
