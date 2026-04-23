#ifndef XPROC_C_H_
#define XPROC_C_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(XPROC_CAPI_SHARED)
#if defined(XPROC_CAPI_BUILDING)
#define XPROC_C_API __declspec(dllexport)
#else
#define XPROC_C_API __declspec(dllimport)
#endif
#else
#define XPROC_C_API
#endif
#elif defined(XPROC_CAPI_SHARED)
#define XPROC_C_API __attribute__((visibility("default")))
#else
#define XPROC_C_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xproc_c_status {
  XPROC_C_STATUS_OK = 0,
  XPROC_C_STATUS_AGAIN = 1,
  XPROC_C_STATUS_BUFFER_TOO_SMALL = 2,
  XPROC_C_STATUS_INVALID_ARGUMENT = 3,
  XPROC_C_STATUS_LOGIC_ERROR = 4,
  XPROC_C_STATUS_LAYOUT_ERROR = 5,
  XPROC_C_STATUS_RUNTIME_ERROR = 6,
  XPROC_C_STATUS_NO_MEMORY = 7,
  XPROC_C_STATUS_INTERNAL_ERROR = 8
} xproc_c_status;

typedef enum xproc_c_endpoint_kind {
  XPROC_C_ENDPOINT_PRODUCER = 0,
  XPROC_C_ENDPOINT_CONSUMER = 1,
  XPROC_C_ENDPOINT_OBSERVER = 2
} xproc_c_endpoint_kind;

typedef enum xproc_c_backend {
  XPROC_C_BACKEND_SHARED_MEMORY = 0,
  XPROC_C_BACKEND_SOCKET = 1
} xproc_c_backend;

typedef enum xproc_c_channel_type {
  XPROC_C_CHANNEL_FIXED = 0,
  XPROC_C_CHANNEL_VARLEN = 1
} xproc_c_channel_type;

typedef enum xproc_c_layout_error {
  XPROC_C_LAYOUT_ERROR_NONE = 0,
  XPROC_C_LAYOUT_ERROR_NOT_ATTACHED = 1,
  XPROC_C_LAYOUT_ERROR_BAD_MAGIC = 2,
  XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT = 3,
  XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH = 4,
  XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH = 5,
  XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH = 6,
  XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH = 7,
  XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH = 8,
  XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID = 9,
  XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT = 10
} xproc_c_layout_error;

typedef struct xproc_c_options {
  xproc_c_backend backend;
  const char* path;
  size_t shm_size;
  uint32_t item_size;
  uint32_t data_align;
  int create_if_missing;
  xproc_c_channel_type channel_type;
  const char* win32_object_namespace;
  const char* socket_host;
  uint16_t socket_port;
  int socket_listen;
  int socket_connect_retries;
  int socket_connect_retry_ms;
} xproc_c_options;

typedef struct xproc_c_snapshot {
  uint64_t write_pos;
  uint64_t read_pos;
  uint32_t commit_seq;
  uint32_t read_wake_seq;
  uint32_t attach_count;
  int32_t producer_pid;
} xproc_c_snapshot;

typedef struct xproc_c_producer xproc_c_producer;
typedef struct xproc_c_consumer xproc_c_consumer;
typedef struct xproc_c_observer xproc_c_observer;

XPROC_C_API void xproc_c_options_init(xproc_c_options* options);
XPROC_C_API const char* xproc_c_status_string(xproc_c_status status);
XPROC_C_API const char* xproc_c_layout_error_string(xproc_c_layout_error error);
XPROC_C_API const char* xproc_c_version_string(void);
XPROC_C_API int32_t xproc_c_current_process_id(void);

/*
 * Validates options for a specific endpoint kind.
 * Observer currently supports shared-memory only.
 */
XPROC_C_API xproc_c_status xproc_c_validate_options_for(xproc_c_endpoint_kind kind, const xproc_c_options* options);

/*
 * Removes a shared-memory segment name on platforms that support unlink semantics.
 * On Windows this is a successful no-op because mappings disappear after the last handle closes.
 */
XPROC_C_API xproc_c_status xproc_c_shm_unlink(const char* path);

/*
 * Returns the last failure message recorded by the calling thread.
 * The returned pointer remains valid until the next xproc_c API call on the same thread.
 */
XPROC_C_API const char* xproc_c_last_error_message(void);

/*
 * Copies the last failure message into buffer and writes the full message length to out_len.
 * Returns BUFFER_TOO_SMALL when the caller should retry with a larger buffer.
 * Query helpers do not clear the stored error state.
 */
XPROC_C_API xproc_c_status xproc_c_last_error_copy(char* buffer, uint32_t buffer_capacity, uint32_t* out_len);
XPROC_C_API xproc_c_layout_error xproc_c_last_layout_error(void);

XPROC_C_API xproc_c_status xproc_c_producer_open(const xproc_c_options* options, xproc_c_producer** out_producer);
XPROC_C_API void xproc_c_producer_close(xproc_c_producer* producer);
/*
 * Fills out_options with borrowed string pointers backed by the handle.
 * String members remain valid until the handle is closed.
 */
XPROC_C_API xproc_c_status xproc_c_producer_options(const xproc_c_producer* producer, xproc_c_options* out_options);
XPROC_C_API xproc_c_status xproc_c_producer_send_fixed_bytes(xproc_c_producer* producer, const void* data,
                                                             uint32_t payload_len);
XPROC_C_API xproc_c_status xproc_c_producer_send_fixed_sized(xproc_c_producer* producer, const void* data,
                                                             uint32_t byte_length);
XPROC_C_API xproc_c_status xproc_c_producer_send_varlen(xproc_c_producer* producer, const void* data, uint32_t len);
XPROC_C_API xproc_c_status xproc_c_producer_socket_port(const xproc_c_producer* producer, uint16_t* out_port);

XPROC_C_API xproc_c_status xproc_c_consumer_open(const xproc_c_options* options, xproc_c_consumer** out_consumer);
XPROC_C_API void xproc_c_consumer_close(xproc_c_consumer* consumer);
XPROC_C_API xproc_c_status xproc_c_consumer_options(const xproc_c_consumer* consumer, xproc_c_options* out_options);
XPROC_C_API xproc_c_status xproc_c_consumer_pending_len(const xproc_c_consumer* consumer, uint32_t* out_len);

/*
 * Returns:
 * - OK when a message was copied into buffer
 * - AGAIN when no message is currently available
 * - BUFFER_TOO_SMALL when out_len is the required size; the message is retained for the next call
 */
XPROC_C_API xproc_c_status xproc_c_consumer_poll_copy(xproc_c_consumer* consumer, void* buffer,
                                                      uint32_t buffer_capacity, uint32_t* out_len);
XPROC_C_API xproc_c_status xproc_c_consumer_wait(xproc_c_consumer* consumer);
XPROC_C_API xproc_c_status xproc_c_consumer_socket_port(const xproc_c_consumer* consumer, uint16_t* out_port);

XPROC_C_API xproc_c_status xproc_c_observer_open(const xproc_c_options* options, xproc_c_observer** out_observer);
XPROC_C_API void xproc_c_observer_close(xproc_c_observer* observer);
XPROC_C_API xproc_c_status xproc_c_observer_options(const xproc_c_observer* observer, xproc_c_options* out_options);
XPROC_C_API xproc_c_status xproc_c_observer_snapshot(const xproc_c_observer* observer, xproc_c_snapshot* out_snapshot);
XPROC_C_API xproc_c_status xproc_c_observer_peek_copy(xproc_c_observer* observer, void* buffer,
                                                      uint32_t buffer_capacity, uint32_t* out_len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XPROC_C_H_
