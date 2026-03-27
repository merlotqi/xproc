#pragma once

#include <memory>
#include <stdexcept>
#include <xproc/ipc/ipc_channel_interface.hpp>
#include <xproc/ipc/ipc_options.hpp>
#include <xproc/ipc/socket_channel.hpp>

namespace xproc {
namespace ipc {

inline std::unique_ptr<IProducerChannel> create_producer_transport(transport_options opts) {
  validate_transport_options(opts);
  switch (opts.backend) {
    case transport_backend::shm:
      return std::make_unique<shm_producer_transport>(opts);
    case transport_backend::socket:
      if (opts.socket_listen) {
        throw std::invalid_argument("create_producer_transport: producer requires socket_listen=false");
      }
      return std::make_unique<socket_producer_transport>(opts);
  }
  throw std::logic_error("create_producer_transport: unknown backend");
}

inline std::unique_ptr<IConsumerChannel> create_consumer_transport(transport_options opts) {
  validate_transport_options(opts);
  switch (opts.backend) {
    case transport_backend::shm:
      return std::make_unique<shm_consumer_transport>(opts);
    case transport_backend::socket:
      if (!opts.socket_listen) {
        throw std::invalid_argument("create_consumer_transport: socket consumer requires socket_listen=true");
      }
      return std::make_unique<socket_consumer_transport>(opts);
  }
  throw std::logic_error("create_consumer_transport: unknown backend");
}

}  // namespace ipc
}  // namespace xproc
