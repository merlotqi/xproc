#pragma once

#include <cstdint>
#include <vector>
#include <xproc/ipc/ipc_channel_interface.hpp>

namespace xproc {
namespace ipc {

// TCP framing: fixed channel sends exactly item_size bytes per message; variable sends uint32_t LE len + payload.
// Consumer listens (socket_listen=true); producer connects (socket_listen=false).
class socket_producer_transport final : public IProducerChannel {
 public:
  explicit socket_producer_transport(const transport_options& opts);
  ~socket_producer_transport() override;

  socket_producer_transport(const socket_producer_transport&) = delete;
  socket_producer_transport& operator=(const socket_producer_transport&) = delete;

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

class socket_consumer_transport final : public IConsumerChannel {
 public:
  explicit socket_consumer_transport(const transport_options& opts);
  ~socket_consumer_transport() override;

  socket_consumer_transport(const socket_consumer_transport&) = delete;
  socket_consumer_transport& operator=(const socket_consumer_transport&) = delete;

  const transport_options& options() const noexcept override { return opts_; }

  void wait_when_empty() override;

 protected:
  bool poll_impl(const std::function<void(void*, std::uint32_t)>& handler) override;

 private:
  transport_options opts_;
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

}  // namespace ipc
}  // namespace xproc
