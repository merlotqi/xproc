#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/socket_channel.hpp>

namespace xproc::ipc {

class socket_listener_builder {
 public:
  socket_listener_builder(channel_type type, std::uint32_t item_size) : type_(type), item_size_(item_size) {}

  socket_listener_builder& with_port(std::uint16_t port) {
    port_ = port;
    return *this;
  }

  transport_options options() const {
    transport_options opts;
    opts.backend = transport_backend::socket;
    opts.type = type_;
    opts.item_size = item_size_;
    opts.socket_listen = true;
    opts.socket_host.clear();
    opts.socket_port = port_;
    validate_consumer_transport_options(opts);
    return opts;
  }

  socket_consumer open_consumer() const { return socket_consumer(options()); }

 private:
  channel_type type_;
  std::uint32_t item_size_{0};
  std::uint16_t port_{0};
};

class socket_connector_builder {
 public:
  socket_connector_builder(channel_type type, std::uint32_t item_size, std::string host, std::uint16_t port)
      : type_(type), item_size_(item_size), host_(std::move(host)), port_(port) {}

  socket_connector_builder& with_connect_retries(int retries) {
    connect_retries_ = retries;
    return *this;
  }

  socket_connector_builder& with_connect_retry_ms(int retry_ms) {
    connect_retry_ms_ = retry_ms;
    return *this;
  }

  transport_options options() const {
    transport_options opts;
    opts.backend = transport_backend::socket;
    opts.type = type_;
    opts.item_size = item_size_;
    opts.socket_listen = false;
    opts.socket_host = host_;
    opts.socket_port = port_;
    opts.socket_connect_retries = connect_retries_;
    opts.socket_connect_retry_ms = connect_retry_ms_;
    validate_producer_transport_options(opts);
    return opts;
  }

  socket_producer open_producer() const { return socket_producer(options()); }

 private:
  channel_type type_;
  std::uint32_t item_size_{0};
  std::string host_;
  std::uint16_t port_{0};
  int connect_retries_{transport_options{}.socket_connect_retries};
  int connect_retry_ms_{transport_options{}.socket_connect_retry_ms};
};

using socket_varlen_listener_builder = socket_listener_builder;
using socket_fixed_listener_builder = socket_listener_builder;
using socket_varlen_connector_builder = socket_connector_builder;
using socket_fixed_connector_builder = socket_connector_builder;

inline socket_varlen_listener_builder listen_varlen_socket() {
  return socket_listener_builder(channel_type::varlen, 0u);
}

inline socket_fixed_listener_builder listen_fixed_socket(std::uint32_t item_size) {
  return socket_listener_builder(channel_type::fixed, item_size);
}

inline socket_varlen_connector_builder connect_varlen_socket(std::string host, std::uint16_t port) {
  return socket_connector_builder(channel_type::varlen, 0u, std::move(host), port);
}

inline socket_fixed_connector_builder connect_fixed_socket(std::string host, std::uint16_t port,
                                                           std::uint32_t item_size) {
  return socket_connector_builder(channel_type::fixed, item_size, std::move(host), port);
}

}  // namespace xproc::ipc
