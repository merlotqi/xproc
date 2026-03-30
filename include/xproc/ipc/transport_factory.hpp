#pragma once

#include <memory>
#include <stdexcept>
#include <xproc/ipc/channel_interface.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/socket_channel.hpp>

namespace xproc {
namespace ipc {

inline std::unique_ptr<producer_channel_interface> create_producer_transport(transport_options opts) {
  validate_transport_options(opts);
  switch (opts.backend) {
    case transport_backend::shared_memory:
      return std::make_unique<shm_producer>(opts);
    case transport_backend::socket:
      if (opts.socket_listen) {
        throw std::invalid_argument("create_producer_transport: producer requires socket_listen=false");
      }
      return std::make_unique<socket_producer>(opts);
  }
  throw std::logic_error("create_producer_transport: unknown backend");
}

inline std::unique_ptr<consumer_channel_interface> create_consumer_transport(transport_options opts) {
  validate_transport_options(opts);
  switch (opts.backend) {
    case transport_backend::shared_memory:
      return std::make_unique<shm_consumer>(opts);
    case transport_backend::socket:
      if (!opts.socket_listen) {
        throw std::invalid_argument("create_consumer_transport: socket consumer requires socket_listen=true");
      }
      return std::make_unique<socket_consumer>(opts);
  }
  throw std::logic_error("create_consumer_transport: unknown backend");
}

}  // namespace ipc
}  // namespace xproc
