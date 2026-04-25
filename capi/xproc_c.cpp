#include "xproc_c.h"

#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <xproc/ipc/channel_interface.hpp>
#include <xproc/ipc/observer.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/transport_factory.hpp>
#include <xproc/platform/process.hpp>
#include <xproc/shm/layout_exception.hpp>

#ifndef XPROC_CAPI_PROJECT_VERSION
#define XPROC_CAPI_PROJECT_VERSION "0.0.0"
#endif

struct xproc_c_producer {
  std::unique_ptr<xproc::ipc::producer_channel_interface> impl;
};

struct xproc_c_consumer {
  std::unique_ptr<xproc::ipc::consumer_channel_interface> impl;
  std::vector<std::uint8_t> pending;
};

struct xproc_c_observer {
  std::unique_ptr<xproc::ipc::observer> impl;
};

namespace {

thread_local std::string g_last_error;
thread_local xproc_c_layout_error g_last_layout_error = XPROC_C_LAYOUT_ERROR_NONE;

template <typename Func>
xproc_c_status catch_status(Func&& func);

xproc_c_status invalid_argument(const char* message);

template <typename Handle>
xproc_c_status validate_handle(const Handle* handle, const char* message);

void clear_last_error() {
  g_last_error.clear();
  g_last_layout_error = XPROC_C_LAYOUT_ERROR_NONE;
}

xproc_c_layout_error to_c_layout_error(xproc::shm::validate_error error) {
  switch (error) {
    case xproc::shm::validate_error::ok:
      return XPROC_C_LAYOUT_ERROR_NONE;
    case xproc::shm::validate_error::not_attached:
      return XPROC_C_LAYOUT_ERROR_NOT_ATTACHED;
    case xproc::shm::validate_error::bad_magic:
      return XPROC_C_LAYOUT_ERROR_BAD_MAGIC;
    case xproc::shm::validate_error::not_ready_timeout:
      return XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT;
    case xproc::shm::validate_error::version_mismatch:
      return XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH;
    case xproc::shm::validate_error::header_size_mismatch:
      return XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH;
    case xproc::shm::validate_error::layout_type_mismatch:
      return XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH;
    case xproc::shm::validate_error::fixed_item_size_mismatch:
      return XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH;
    case xproc::shm::validate_error::schema_id_mismatch:
      return XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH;
    case xproc::shm::validate_error::alignment_invalid:
      return XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID;
    case xproc::shm::validate_error::capacity_insufficient:
      return XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT;
  }
  return XPROC_C_LAYOUT_ERROR_NONE;
}

xproc_c_status set_last_error(xproc_c_status status, std::string message,
                              xproc_c_layout_error layout_error = XPROC_C_LAYOUT_ERROR_NONE) {
  g_last_error = std::move(message);
  g_last_layout_error = layout_error;
  return status;
}

xproc::ipc::transport_options to_cpp_options(const xproc_c_options& options) {
  xproc::ipc::transport_options out;
  out.backend = (options.backend == XPROC_C_BACKEND_SOCKET) ? xproc::ipc::transport_backend::socket
                                                            : xproc::ipc::transport_backend::shared_memory;
  if (options.path != nullptr) {
    out.path = options.path;
  }
  out.shm_size = options.shm_size;
  out.item_size = options.item_size;
  out.data_align = options.data_align;
  out.schema_id = options.schema_id;
  out.creator_timestamp_ns = options.creator_timestamp_ns;
  out.creator_flags = options.creator_flags;
  out.create_if_missing = (options.create_if_missing != 0);
  out.type = (options.channel_type == XPROC_C_CHANNEL_VARLEN) ? xproc::ipc::channel_type::varlen
                                                              : xproc::ipc::channel_type::fixed;
  out.win32_object_namespace = (options.win32_object_namespace != nullptr) ? options.win32_object_namespace : "Local";
  out.socket_host = (options.socket_host != nullptr) ? options.socket_host : "127.0.0.1";
  out.socket_port = options.socket_port;
  out.socket_listen = (options.socket_listen != 0);
  out.socket_connect_retries = options.socket_connect_retries;
  out.socket_connect_retry_ms = options.socket_connect_retry_ms;
  return out;
}

void fill_borrowed_options(const xproc::ipc::transport_options& options, xproc_c_options* out) {
  out->backend = (options.backend == xproc::ipc::transport_backend::socket) ? XPROC_C_BACKEND_SOCKET
                                                                            : XPROC_C_BACKEND_SHARED_MEMORY;
  out->path = options.path.empty() ? nullptr : options.path.c_str();
  out->shm_size = options.shm_size;
  out->item_size = options.item_size;
  out->data_align = options.data_align;
  out->schema_id = options.schema_id;
  out->creator_timestamp_ns = options.creator_timestamp_ns;
  out->creator_flags = options.creator_flags;
  out->create_if_missing = options.create_if_missing ? 1 : 0;
  out->channel_type =
      (options.type == xproc::ipc::channel_type::varlen) ? XPROC_C_CHANNEL_VARLEN : XPROC_C_CHANNEL_FIXED;
  out->win32_object_namespace =
      options.win32_object_namespace.empty() ? nullptr : options.win32_object_namespace.c_str();
  out->socket_host = options.socket_host.empty() ? nullptr : options.socket_host.c_str();
  out->socket_port = options.socket_port;
  out->socket_listen = options.socket_listen ? 1 : 0;
  out->socket_connect_retries = options.socket_connect_retries;
  out->socket_connect_retry_ms = options.socket_connect_retry_ms;
}

xproc_c_status validate_endpoint_kind(xproc_c_endpoint_kind kind) {
  switch (kind) {
    case XPROC_C_ENDPOINT_PRODUCER:
    case XPROC_C_ENDPOINT_CONSUMER:
    case XPROC_C_ENDPOINT_OBSERVER:
      return XPROC_C_STATUS_OK;
  }
  return invalid_argument("xproc_c: unknown endpoint kind");
}

template <typename T>
xproc_c_status validate_out_pointer(T* ptr, const char* message) {
  if (ptr == nullptr) {
    return invalid_argument(message);
  }
  return XPROC_C_STATUS_OK;
}

template <typename Handle>
xproc_c_status validate_options_query_args(const Handle* handle, xproc_c_options* out_options,
                                           const char* handle_message, const char* out_message) {
  const xproc_c_status out_status = validate_out_pointer(out_options, out_message);
  if (out_status != XPROC_C_STATUS_OK) {
    return out_status;
  }
  return validate_handle(handle, handle_message);
}

template <typename Handle>
xproc_c_status copy_options_from_handle(const Handle* handle, xproc_c_options* out_options, const char* handle_message,
                                        const char* out_message) {
  const xproc_c_status status = validate_options_query_args(handle, out_options, handle_message, out_message);
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  fill_borrowed_options(handle->impl->options(), out_options);
  clear_last_error();
  return XPROC_C_STATUS_OK;
}

template <>
xproc_c_status copy_options_from_handle(const xproc_c_observer* handle, xproc_c_options* out_options,
                                        const char* handle_message, const char* out_message) {
  const xproc_c_status status = validate_options_query_args(handle, out_options, handle_message, out_message);
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  fill_borrowed_options(handle->impl->options(), out_options);
  clear_last_error();
  return XPROC_C_STATUS_OK;
}

xproc_c_status validate_options_for_kind(xproc_c_endpoint_kind kind, const xproc_c_options* options) {
  const xproc_c_status kind_status = validate_endpoint_kind(kind);
  if (kind_status != XPROC_C_STATUS_OK) {
    return kind_status;
  }
  if (options == nullptr) {
    return invalid_argument("xproc_c_validate_options_for: options must not be null");
  }

  return catch_status([&]() -> xproc_c_status {
    const xproc::ipc::transport_options cpp_options = to_cpp_options(*options);
    switch (kind) {
      case XPROC_C_ENDPOINT_PRODUCER:
        xproc::ipc::validate_producer_transport_options(cpp_options);
        break;
      case XPROC_C_ENDPOINT_CONSUMER:
        xproc::ipc::validate_consumer_transport_options(cpp_options);
        break;
      case XPROC_C_ENDPOINT_OBSERVER:
        xproc::ipc::validate_observer_transport_options(cpp_options);
        break;
    }

    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

template <typename Func>
xproc_c_status catch_status(Func&& func) {
  try {
    return std::forward<Func>(func)();
  } catch (const xproc::shm::layout_exception& ex) {
    return set_last_error(XPROC_C_STATUS_LAYOUT_ERROR, ex.what(), to_c_layout_error(ex.code()));
  } catch (const std::invalid_argument& ex) {
    return set_last_error(XPROC_C_STATUS_INVALID_ARGUMENT, ex.what());
  } catch (const std::logic_error& ex) {
    return set_last_error(XPROC_C_STATUS_LOGIC_ERROR, ex.what());
  } catch (const std::runtime_error& ex) {
    return set_last_error(XPROC_C_STATUS_RUNTIME_ERROR, ex.what());
  } catch (const std::bad_alloc&) {
    return set_last_error(XPROC_C_STATUS_NO_MEMORY, "xproc_c: allocation failed");
  } catch (const std::exception& ex) {
    return set_last_error(XPROC_C_STATUS_INTERNAL_ERROR, ex.what());
  } catch (...) {
    return set_last_error(XPROC_C_STATUS_INTERNAL_ERROR, "xproc_c: unknown exception");
  }
}

xproc_c_status invalid_argument(const char* message) {
  return set_last_error(XPROC_C_STATUS_INVALID_ARGUMENT, message);
}

template <typename Handle>
xproc_c_status validate_handle(const Handle* handle, const char* message) {
  if (handle == nullptr || handle->impl == nullptr) {
    return invalid_argument(message);
  }
  return XPROC_C_STATUS_OK;
}

xproc_c_status validate_copy_args(const void* buffer, std::uint32_t buffer_capacity, std::uint32_t* out_len,
                                  const char* out_len_message) {
  if (out_len == nullptr) {
    return invalid_argument(out_len_message);
  }
  if (buffer == nullptr && buffer_capacity != 0u) {
    *out_len = 0;
    return invalid_argument("xproc_c: buffer is null while buffer_capacity is non-zero");
  }
  return XPROC_C_STATUS_OK;
}

template <typename ProducerHandle>
xproc_c_status producer_socket_port_impl(const ProducerHandle* producer, std::uint16_t* out_port,
                                         const char* invalid_message) {
  if (out_port == nullptr) {
    return invalid_argument("xproc_c: out_port must not be null");
  }
  const xproc_c_status status = validate_handle(producer, invalid_message);
  if (status != XPROC_C_STATUS_OK) {
    *out_port = 0;
    return status;
  }
  *out_port = producer->impl->options().socket_port;
  clear_last_error();
  return XPROC_C_STATUS_OK;
}

}  // namespace

void xproc_c_options_init(xproc_c_options* options) {
  if (options == nullptr) {
    return;
  }
  options->backend = XPROC_C_BACKEND_SHARED_MEMORY;
  options->path = nullptr;
  options->shm_size = 0;
  options->item_size = 0;
  options->data_align = 0;
  options->schema_id = 0;
  options->creator_timestamp_ns = 0;
  options->creator_flags = 0;
  options->create_if_missing = 1;
  options->channel_type = XPROC_C_CHANNEL_FIXED;
  options->win32_object_namespace = "Local";
  options->socket_host = "127.0.0.1";
  options->socket_port = 0;
  options->socket_listen = 0;
  options->socket_connect_retries = 200;
  options->socket_connect_retry_ms = 10;
}

std::size_t xproc_c_shm_size_for_data_capacity(std::size_t data_capacity) {
  return xproc::ipc::shm_size_for_data_capacity(data_capacity);
}

std::size_t xproc_c_shm_data_capacity_for_size(std::size_t shm_size) {
  return xproc::ipc::shm_data_capacity_for_size(shm_size);
}

const char* xproc_c_status_string(xproc_c_status status) {
  switch (status) {
    case XPROC_C_STATUS_OK:
      return "ok";
    case XPROC_C_STATUS_AGAIN:
      return "again";
    case XPROC_C_STATUS_BUFFER_TOO_SMALL:
      return "buffer_too_small";
    case XPROC_C_STATUS_INVALID_ARGUMENT:
      return "invalid_argument";
    case XPROC_C_STATUS_LOGIC_ERROR:
      return "logic_error";
    case XPROC_C_STATUS_LAYOUT_ERROR:
      return "layout_error";
    case XPROC_C_STATUS_RUNTIME_ERROR:
      return "runtime_error";
    case XPROC_C_STATUS_NO_MEMORY:
      return "no_memory";
    case XPROC_C_STATUS_INTERNAL_ERROR:
      return "internal_error";
  }
  return "unknown_status";
}

const char* xproc_c_layout_error_string(xproc_c_layout_error error) {
  switch (error) {
    case XPROC_C_LAYOUT_ERROR_NONE:
      return "none";
    case XPROC_C_LAYOUT_ERROR_NOT_ATTACHED:
      return "not_attached";
    case XPROC_C_LAYOUT_ERROR_BAD_MAGIC:
      return "bad_magic";
    case XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT:
      return "not_ready_timeout";
    case XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH:
      return "version_mismatch";
    case XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH:
      return "header_size_mismatch";
    case XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH:
      return "layout_type_mismatch";
    case XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH:
      return "fixed_item_size_mismatch";
    case XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH:
      return "schema_id_mismatch";
    case XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID:
      return "alignment_invalid";
    case XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT:
      return "capacity_insufficient";
  }
  return "unknown_layout_error";
}

const char* xproc_c_version_string(void) { return XPROC_CAPI_PROJECT_VERSION; }

std::int32_t xproc_c_current_process_id(void) { return xproc::platform::current_process_id(); }

const char* xproc_c_last_error_message(void) { return g_last_error.c_str(); }

xproc_c_status xproc_c_validate_options_for(xproc_c_endpoint_kind kind, const xproc_c_options* options) {
  return validate_options_for_kind(kind, options);
}

xproc_c_status xproc_c_shm_unlink(const char* path) {
  if (path == nullptr) {
    return invalid_argument("xproc_c_shm_unlink: path must not be null");
  }
  xproc::shm::shm::unlink(path);
  clear_last_error();
  return XPROC_C_STATUS_OK;
}

xproc_c_status xproc_c_last_error_copy(char* buffer, std::uint32_t buffer_capacity, std::uint32_t* out_len) {
  if (out_len == nullptr) {
    return invalid_argument("xproc_c_last_error_copy: out_len must not be null");
  }
  if (buffer == nullptr && buffer_capacity != 0u) {
    *out_len = 0;
    return invalid_argument("xproc_c_last_error_copy: buffer is null while buffer_capacity is non-zero");
  }

  *out_len = static_cast<std::uint32_t>(g_last_error.size());
  if (buffer_capacity < *out_len) {
    return XPROC_C_STATUS_BUFFER_TOO_SMALL;
  }
  if (*out_len != 0u) {
    std::memcpy(buffer, g_last_error.data(), *out_len);
  }
  return XPROC_C_STATUS_OK;
}

xproc_c_layout_error xproc_c_last_layout_error(void) { return g_last_layout_error; }

xproc_c_status xproc_c_producer_open(const xproc_c_options* options, xproc_c_producer** out_producer) {
  if (out_producer == nullptr) {
    return invalid_argument("xproc_c_producer_open: out_producer must not be null");
  }
  *out_producer = nullptr;
  if (options == nullptr) {
    return invalid_argument("xproc_c_producer_open: options must not be null");
  }
  {
    const xproc_c_status validate_status = validate_options_for_kind(XPROC_C_ENDPOINT_PRODUCER, options);
    if (validate_status != XPROC_C_STATUS_OK) {
      return validate_status;
    }
  }

  return catch_status([&]() -> xproc_c_status {
    auto handle = std::make_unique<xproc_c_producer>();
    handle->impl = xproc::ipc::create_producer_transport(to_cpp_options(*options));
    *out_producer = handle.release();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

void xproc_c_producer_close(xproc_c_producer* producer) { delete producer; }

xproc_c_status xproc_c_producer_options(const xproc_c_producer* producer, xproc_c_options* out_options) {
  return copy_options_from_handle(producer, out_options, "xproc_c_producer_options: producer is null",
                                  "xproc_c_producer_options: out_options must not be null");
}

xproc_c_status xproc_c_producer_send_fixed_bytes(xproc_c_producer* producer, const void* data,
                                                 std::uint32_t payload_len) {
  const xproc_c_status status = validate_handle(producer, "xproc_c_producer_send_fixed_bytes: producer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  if (data == nullptr && payload_len != 0u) {
    return invalid_argument("xproc_c_producer_send_fixed_bytes: data is null while payload_len is non-zero");
  }

  return catch_status([&]() -> xproc_c_status {
    producer->impl->send_fixed_bytes(data, payload_len);
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_producer_send_fixed_sized(xproc_c_producer* producer, const void* data,
                                                 std::uint32_t byte_length) {
  const xproc_c_status status = validate_handle(producer, "xproc_c_producer_send_fixed_sized: producer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  if (data == nullptr && byte_length != 0u) {
    return invalid_argument("xproc_c_producer_send_fixed_sized: data is null while byte_length is non-zero");
  }

  return catch_status([&]() -> xproc_c_status {
    producer->impl->send_fixed_sized(data, byte_length);
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_producer_send_varlen(xproc_c_producer* producer, const void* data, std::uint32_t len) {
  const xproc_c_status status = validate_handle(producer, "xproc_c_producer_send_varlen: producer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  if (data == nullptr && len != 0u) {
    return invalid_argument("xproc_c_producer_send_varlen: data is null while len is non-zero");
  }

  return catch_status([&]() -> xproc_c_status {
    producer->impl->send_varlen(data, len);
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_producer_socket_port(const xproc_c_producer* producer, std::uint16_t* out_port) {
  return producer_socket_port_impl(producer, out_port, "xproc_c_producer_socket_port: producer is null");
}

xproc_c_status xproc_c_consumer_open(const xproc_c_options* options, xproc_c_consumer** out_consumer) {
  if (out_consumer == nullptr) {
    return invalid_argument("xproc_c_consumer_open: out_consumer must not be null");
  }
  *out_consumer = nullptr;
  if (options == nullptr) {
    return invalid_argument("xproc_c_consumer_open: options must not be null");
  }
  {
    const xproc_c_status validate_status = validate_options_for_kind(XPROC_C_ENDPOINT_CONSUMER, options);
    if (validate_status != XPROC_C_STATUS_OK) {
      return validate_status;
    }
  }

  return catch_status([&]() -> xproc_c_status {
    auto handle = std::make_unique<xproc_c_consumer>();
    handle->impl = xproc::ipc::create_consumer_transport(to_cpp_options(*options));
    *out_consumer = handle.release();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

void xproc_c_consumer_close(xproc_c_consumer* consumer) { delete consumer; }

xproc_c_status xproc_c_consumer_options(const xproc_c_consumer* consumer, xproc_c_options* out_options) {
  return copy_options_from_handle(consumer, out_options, "xproc_c_consumer_options: consumer is null",
                                  "xproc_c_consumer_options: out_options must not be null");
}

xproc_c_status xproc_c_consumer_pending_len(const xproc_c_consumer* consumer, std::uint32_t* out_len) {
  const xproc_c_status out_status =
      validate_out_pointer(out_len, "xproc_c_consumer_pending_len: out_len must not be null");
  if (out_status != XPROC_C_STATUS_OK) {
    return out_status;
  }
  const xproc_c_status handle_status = validate_handle(consumer, "xproc_c_consumer_pending_len: consumer is null");
  if (handle_status != XPROC_C_STATUS_OK) {
    *out_len = 0;
    return handle_status;
  }
  *out_len = static_cast<std::uint32_t>(consumer->pending.size());
  clear_last_error();
  return XPROC_C_STATUS_OK;
}

xproc_c_status xproc_c_consumer_poll_copy(xproc_c_consumer* consumer, void* buffer, std::uint32_t buffer_capacity,
                                          std::uint32_t* out_len) {
  const xproc_c_status args_status =
      validate_copy_args(buffer, buffer_capacity, out_len, "xproc_c_consumer_poll_copy: out_len must not be null");
  if (args_status != XPROC_C_STATUS_OK) {
    return args_status;
  }
  const xproc_c_status handle_status = validate_handle(consumer, "xproc_c_consumer_poll_copy: consumer is null");
  if (handle_status != XPROC_C_STATUS_OK) {
    *out_len = 0;
    return handle_status;
  }

  if (!consumer->pending.empty()) {
    *out_len = static_cast<std::uint32_t>(consumer->pending.size());
    if (buffer_capacity < *out_len) {
      return set_last_error(XPROC_C_STATUS_BUFFER_TOO_SMALL, "xproc_c_consumer_poll_copy: buffer too small");
    }
    if (*out_len != 0u) {
      std::memcpy(buffer, consumer->pending.data(), *out_len);
    }
    consumer->pending.clear();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  }

  return catch_status([&]() -> xproc_c_status {
    bool too_small = false;
    std::uint32_t message_len = 0;
    std::vector<std::uint8_t> pending;

    const bool has_message = consumer->impl->poll([&](void* ptr, std::uint32_t len) {
      message_len = len;
      if (len > buffer_capacity) {
        too_small = true;
        pending.resize(len);
        if (len != 0u) {
          std::memcpy(pending.data(), ptr, len);
        }
        return;
      }
      if (len != 0u) {
        std::memcpy(buffer, ptr, len);
      }
    });

    if (!has_message) {
      *out_len = 0;
      clear_last_error();
      return XPROC_C_STATUS_AGAIN;
    }

    *out_len = message_len;
    if (too_small) {
      consumer->pending = std::move(pending);
      return set_last_error(XPROC_C_STATUS_BUFFER_TOO_SMALL, "xproc_c_consumer_poll_copy: buffer too small");
    }

    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_consumer_wait(xproc_c_consumer* consumer) {
  const xproc_c_status status = validate_handle(consumer, "xproc_c_consumer_wait: consumer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }
  if (!consumer->pending.empty()) {
    clear_last_error();
    return XPROC_C_STATUS_OK;
  }

  return catch_status([&]() -> xproc_c_status {
    consumer->impl->wait();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_consumer_socket_port(const xproc_c_consumer* consumer, std::uint16_t* out_port) {
  return producer_socket_port_impl(consumer, out_port, "xproc_c_consumer_socket_port: consumer is null");
}

xproc_c_status xproc_c_observer_open(const xproc_c_options* options, xproc_c_observer** out_observer) {
  if (out_observer == nullptr) {
    return invalid_argument("xproc_c_observer_open: out_observer must not be null");
  }
  *out_observer = nullptr;
  if (options == nullptr) {
    return invalid_argument("xproc_c_observer_open: options must not be null");
  }
  {
    const xproc_c_status validate_status = validate_options_for_kind(XPROC_C_ENDPOINT_OBSERVER, options);
    if (validate_status != XPROC_C_STATUS_OK) {
      return validate_status;
    }
  }

  return catch_status([&]() -> xproc_c_status {
    auto handle = std::make_unique<xproc_c_observer>();
    handle->impl = std::make_unique<xproc::ipc::observer>(to_cpp_options(*options));
    *out_observer = handle.release();
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

void xproc_c_observer_close(xproc_c_observer* observer) { delete observer; }

xproc_c_status xproc_c_observer_options(const xproc_c_observer* observer, xproc_c_options* out_options) {
  return copy_options_from_handle(observer, out_options, "xproc_c_observer_options: observer is null",
                                  "xproc_c_observer_options: out_options must not be null");
}

xproc_c_status xproc_c_observer_snapshot(const xproc_c_observer* observer, xproc_c_snapshot* out_snapshot) {
  if (out_snapshot == nullptr) {
    return invalid_argument("xproc_c_observer_snapshot: out_snapshot must not be null");
  }
  const xproc_c_status status = validate_handle(observer, "xproc_c_observer_snapshot: observer is null");
  if (status != XPROC_C_STATUS_OK) {
    return status;
  }

  return catch_status([&]() -> xproc_c_status {
    const xproc::ipc::ring_snapshot snapshot = observer->impl->snapshot();
    out_snapshot->write_pos = snapshot.write_pos;
    out_snapshot->read_pos = snapshot.read_pos;
    out_snapshot->commit_seq = snapshot.commit_seq;
    out_snapshot->read_wake_seq = snapshot.read_wake_seq;
    out_snapshot->attach_count = snapshot.attach_count;
    out_snapshot->producer_pid = snapshot.producer_pid;
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}

xproc_c_status xproc_c_observer_peek_copy(xproc_c_observer* observer, void* buffer, std::uint32_t buffer_capacity,
                                          std::uint32_t* out_len) {
  const xproc_c_status args_status =
      validate_copy_args(buffer, buffer_capacity, out_len, "xproc_c_observer_peek_copy: out_len must not be null");
  if (args_status != XPROC_C_STATUS_OK) {
    return args_status;
  }
  const xproc_c_status handle_status = validate_handle(observer, "xproc_c_observer_peek_copy: observer is null");
  if (handle_status != XPROC_C_STATUS_OK) {
    *out_len = 0;
    return handle_status;
  }

  return catch_status([&]() -> xproc_c_status {
    bool too_small = false;
    std::uint32_t message_len = 0;

    const bool has_message = observer->impl->peek([&](const void* ptr, std::uint32_t len) {
      message_len = len;
      if (len > buffer_capacity) {
        too_small = true;
        return;
      }
      if (len != 0u) {
        std::memcpy(buffer, ptr, len);
      }
    });

    if (!has_message) {
      *out_len = 0;
      clear_last_error();
      return XPROC_C_STATUS_AGAIN;
    }

    *out_len = message_len;
    if (too_small) {
      return set_last_error(XPROC_C_STATUS_BUFFER_TOO_SMALL, "xproc_c_observer_peek_copy: buffer too small");
    }

    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}
