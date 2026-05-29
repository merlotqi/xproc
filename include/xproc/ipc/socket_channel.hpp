#pragma once

#include <cstdint>
#include <memory>
#include <xproc/ipc/channel_interface.hpp>

namespace xproc::ipc {

class socket_wait_interruptor;

// TCP framing: fixed channel sends exactly item_size bytes per message; variable sends uint32_t LE len + payload.
// Producer connect resolves IPv4 / IPv6 via getaddrinfo(AF_UNSPEC). Consumer listen prefers an IPv6 socket with
// IPV6_V6ONLY disabled so one listener can accept both IPv6 and IPv4-mapped peers when the platform allows it,
// falling back to IPv4 when dual-stack binding is unavailable.
class socket_producer final : public producer_channel_interface {
 public:
  explicit socket_producer(const transport_options& opts);
  ~socket_producer() override;

  socket_producer(const socket_producer&) = delete;
  socket_producer& operator=(const socket_producer&) = delete;

  const transport_options& options() const noexcept override { return opts_; }

  void send_fixed_bytes(const void* data, std::uint32_t payload_len) override;
  void send_fixed_sized(const void* data, std::uint32_t byte_length) override;
  void send_varlen(const void* data, std::uint32_t len) override;

 private:
  transport_options opts_;
#if defined(_WIN32)
  std::uintptr_t sock_{static_cast<std::uintptr_t>(-1)};
#else
  int sock_{-1};
#endif
  void close_sock() noexcept;
  void write_full(const void* data, std::size_t len);
};

class socket_consumer final : public consumer_channel_interface {
 public:
  explicit socket_consumer(const transport_options& opts);
  ~socket_consumer() override;

  socket_consumer(const socket_consumer&) = delete;
  socket_consumer& operator=(const socket_consumer&) = delete;

  const transport_options& options() const noexcept override { return opts_; }

  bool is_connected() const noexcept;
  void wait() override;
  void interrupt_wait() noexcept override;

 protected:
  bool poll_impl(const std::function<void(void*, std::uint32_t)>& handler) override;

 private:
  transport_options opts_;
  std::unique_ptr<socket_wait_interruptor> wake_;
#if defined(_WIN32)
  std::uintptr_t listen_{static_cast<std::uintptr_t>(-1)};
  std::uintptr_t sock_{static_cast<std::uintptr_t>(-1)};
#else
  int listen_{-1};
  int sock_{-1};
#endif
  void close_sock() noexcept;
  void close_listen() noexcept;
  bool ensure_peer_connected();
};

}  // namespace xproc::ipc
