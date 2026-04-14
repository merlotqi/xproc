#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <xproc/shm/shm_layout.hpp>

namespace xproc::ipc {

enum class channel_type {
  fixed,
  varlen
};

enum class transport_backend {
  shared_memory,
  socket
};

struct transport_options {
  transport_backend backend = transport_backend::shared_memory;
  std::string path;
  size_t shm_size = 0;
  uint32_t item_size = 0;
  uint32_t data_align = 0;
  bool create_if_missing = true;
  channel_type type = channel_type::fixed;
  /// Windows only: object namespace prefix for the named section ("Local" or "Global"). Ignored on Linux.
  std::string win32_object_namespace = "Local";
  /// TCP transport: IPv4/6 host; consumer binds all interfaces when socket_listen is true (see socket backend).
  std::string socket_host = "127.0.0.1";
  std::uint16_t socket_port = 0;
  /// true = consumer binds and accepts; false = producer connects to socket_host:socket_port.
  bool socket_listen = false;
  /// Socket transport: max connection retry attempts for producer (0 = unlimited until connected).
  int socket_connect_retries = 200;
  /// Socket transport: milliseconds between connection retries.
  int socket_connect_retry_ms = 10;
};

constexpr std::size_t min_shm_size = sizeof(shm::control_block);

// Central validation for endpoint / channel / ipc_observer / transports. Throws std::invalid_argument.
inline void validate_transport_options(const transport_options& opts) {
  if (opts.type == channel_type::fixed && opts.item_size == 0) {
    throw std::invalid_argument("transport_options: fixed channel requires non-zero item_size");
  }
  if (opts.data_align != 0) {
    const uint32_t a = opts.data_align;
    if (a < 4u || (a & (a - 1u)) != 0u) {
      throw std::invalid_argument("transport_options: data_align must be 0 (default 8) or a power of two >= 4");
    }
  }
#if defined(_WIN32)
  if (opts.win32_object_namespace != "Local" && opts.win32_object_namespace != "Global") {
    throw std::invalid_argument("transport_options: win32_object_namespace must be \"Local\" (default) or \"Global\"");
  }
#endif

  if (opts.backend == transport_backend::shared_memory) {
    if (opts.path.empty()) {
      throw std::invalid_argument("transport_options: shared_memory backend requires non-empty path");
    }
    if (opts.shm_size < min_shm_size) {
      throw std::invalid_argument("transport_options: shm_size is smaller than control_block");
    }
    return;
  }

  if (opts.backend == transport_backend::socket) {
    if (opts.socket_connect_retries < 0) {
      throw std::invalid_argument("transport_options: socket_connect_retries must be >= 0 (0 = unlimited)");
    }
    if (opts.socket_connect_retry_ms < 0) {
      throw std::invalid_argument("transport_options: socket_connect_retry_ms must be >= 0");
    }
    if (!opts.socket_listen && opts.socket_host.empty()) {
      throw std::invalid_argument("transport_options: socket connect mode requires non-empty socket_host");
    }
    if (!opts.socket_listen && opts.socket_port == 0) {
      throw std::invalid_argument("transport_options: socket connect mode requires non-zero socket_port");
    }
    return;
  }
}

inline void validate_producer_transport_options(const transport_options& opts) {
  validate_transport_options(opts);
  if (opts.backend == transport_backend::socket && opts.socket_listen) {
    throw std::invalid_argument("transport_options: socket producer requires socket_listen=false");
  }
}

inline void validate_consumer_transport_options(const transport_options& opts) {
  validate_transport_options(opts);
  if (opts.backend == transport_backend::socket && !opts.socket_listen) {
    throw std::invalid_argument("transport_options: socket consumer requires socket_listen=true");
  }
}

inline void validate_observer_transport_options(const transport_options& opts) {
  validate_transport_options(opts);
  if (opts.backend != transport_backend::shared_memory) {
    throw std::invalid_argument("transport_options: observer requires shared_memory backend");
  }
}

}  // namespace xproc::ipc
