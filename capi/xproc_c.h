#ifndef XPROC_C_H_
#define XPROC_C_H_

#include <stddef.h>
#include <stdint.h>

#define XPROC_C_INFER_EXISTING_SHM_SIZE ((size_t)0)

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
  /* Shared-memory backend:
   * - creator / create_if_missing=1: total bytes including the control block
   * - attacher / existing segment: XPROC_C_INFER_EXISTING_SHM_SIZE infers the created size
   */
  size_t shm_size;
  uint32_t item_size;
  uint32_t data_align;
  /* Optional shared-memory manifest field for protocol / schema compatibility checks. */
  uint64_t schema_id;
  /* Optional shared-memory manifest fields persisted by the creator for application metadata. */
  uint64_t creator_timestamp_ns;
  uint64_t creator_flags;
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

/**
 * @brief Fills an option struct with library defaults.
 *
 * @param options Destination struct to initialize. Passing NULL is a no-op.
 */
XPROC_C_API void xproc_c_options_init(xproc_c_options* options);

/**
 * @brief Computes the total shared-memory size needed for a payload region.
 *
 * The returned size includes the internal xproc control block plus the requested
 * payload capacity.
 *
 * @param data_capacity Payload bytes that should be usable by the ring buffer.
 * @return Total shared-memory size in bytes.
 */
XPROC_C_API size_t xproc_c_shm_size_for_data_capacity(size_t data_capacity);

/**
 * @brief Computes the payload capacity represented by a total shared-memory size.
 *
 * @param shm_size Total shared-memory size in bytes.
 * @return Usable payload bytes after subtracting the control block.
 */
XPROC_C_API size_t xproc_c_shm_data_capacity_for_size(size_t shm_size);

/**
 * @brief Returns a stable string for a status code.
 *
 * @param status Status value to stringify.
 * @return Null-terminated constant string.
 */
XPROC_C_API const char* xproc_c_status_string(xproc_c_status status);

/**
 * @brief Returns a stable string for a layout validation error.
 *
 * @param error Layout error value to stringify.
 * @return Null-terminated constant string.
 */
XPROC_C_API const char* xproc_c_layout_error_string(xproc_c_layout_error error);

/**
 * @brief Returns the xproc C API build version string.
 *
 * @return Null-terminated constant version string.
 */
XPROC_C_API const char* xproc_c_version_string(void);

/**
 * @brief Returns the current process identifier as seen by xproc.
 *
 * @return Process id for the calling process.
 */
XPROC_C_API int32_t xproc_c_current_process_id(void);

/**
 * @brief Validates options for a specific endpoint kind.
 *
 * Observer endpoints currently support the shared-memory backend only.
 *
 * @param kind Endpoint kind to validate for.
 * @param options Options to validate.
 * @return XPROC_C_STATUS_OK when valid, otherwise an error status and thread-local diagnostics.
 */
XPROC_C_API xproc_c_status xproc_c_validate_options_for(xproc_c_endpoint_kind kind, const xproc_c_options* options);

/**
 * @brief Removes a named shared-memory segment when the platform supports unlink semantics.
 *
 * On Windows this succeeds as a no-op because mappings disappear after the last
 * handle closes.
 *
 * @param path Shared-memory object name.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_shm_unlink(const char* path);

/**
 * @brief Returns the last failure message recorded on the calling thread.
 *
 * The returned pointer remains valid until the next xproc_c API call on the same
 * thread.
 *
 * @return Borrowed null-terminated error string, or an empty string when no error is stored.
 */
XPROC_C_API const char* xproc_c_last_error_message(void);

/**
 * @brief Copies the last thread-local error message into a caller-provided buffer.
 *
 * Query helpers do not clear the stored error state.
 *
 * @param buffer Destination buffer. May be NULL when @p buffer_capacity is zero.
 * @param buffer_capacity Size of @p buffer in bytes.
 * @param out_len Receives the full message length, excluding the trailing NUL terminator.
 * @return XPROC_C_STATUS_OK on success, or XPROC_C_STATUS_BUFFER_TOO_SMALL when the caller should retry with a larger
 * buffer.
 */
XPROC_C_API xproc_c_status xproc_c_last_error_copy(char* buffer, uint32_t buffer_capacity, uint32_t* out_len);

/**
 * @brief Returns the last layout validation error recorded on the calling thread.
 *
 * @return The last layout validation error, or XPROC_C_LAYOUT_ERROR_NONE.
 */
XPROC_C_API xproc_c_layout_error xproc_c_last_layout_error(void);

/**
 * @brief Opens a producer endpoint.
 *
 * @param options Producer options to use.
 * @param out_producer Receives the created handle on success. Set to NULL on failure.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_producer_open(const xproc_c_options* options, xproc_c_producer** out_producer);

/**
 * @brief Closes a producer handle.
 *
 * Passing NULL is allowed.
 *
 * @param producer Producer handle to destroy.
 */
XPROC_C_API void xproc_c_producer_close(xproc_c_producer* producer);

/**
 * @brief Returns the options associated with a producer handle.
 *
 * String members in @p out_options are borrowed from the handle and remain valid
 * until the handle is closed.
 *
 * @param producer Producer handle to inspect.
 * @param out_options Receives the borrowed option view.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_producer_options(const xproc_c_producer* producer, xproc_c_options* out_options);

/**
 * @brief Sends one fixed-size slot, zero-padding any unused tail bytes.
 *
 * This API is only valid for fixed channels. The payload length must not exceed
 * the configured item size.
 *
 * @param producer Producer handle.
 * @param data Payload bytes. May be NULL only when @p payload_len is zero.
 * @param payload_len Number of payload bytes to copy before zero-padding.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_producer_send_fixed_bytes(xproc_c_producer* producer, const void* data,
                                                             uint32_t payload_len);

/**
 * @brief Sends one fixed-channel message using exactly the requested byte length.
 *
 * This API is only valid for fixed channels. The byte length must not exceed the
 * configured item size.
 *
 * @param producer Producer handle.
 * @param data Message bytes. May be NULL only when @p byte_length is zero.
 * @param byte_length Number of bytes to copy into the reserved fixed-channel slot.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_producer_send_fixed_sized(xproc_c_producer* producer, const void* data,
                                                             uint32_t byte_length);

/**
 * @brief Sends one variable-length message.
 *
 * @param producer Producer handle.
 * @param data Message bytes. May be NULL only when @p len is zero.
 * @param len Number of bytes to send.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_producer_send_varlen(xproc_c_producer* producer, const void* data, uint32_t len);

/**
 * @brief Returns the socket port currently associated with a producer handle.
 *
 * This is primarily useful for socket transports that bind or resolve a port at runtime.
 *
 * @param producer Producer handle.
 * @param out_port Receives the current port.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_producer_socket_port(const xproc_c_producer* producer, uint16_t* out_port);

/**
 * @brief Opens a consumer endpoint.
 *
 * @param options Consumer options to use.
 * @param out_consumer Receives the created handle on success. Set to NULL on failure.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_consumer_open(const xproc_c_options* options, xproc_c_consumer** out_consumer);

/**
 * @brief Closes a consumer handle.
 *
 * Passing NULL is allowed.
 *
 * @param consumer Consumer handle to destroy.
 */
XPROC_C_API void xproc_c_consumer_close(xproc_c_consumer* consumer);

/**
 * @brief Returns the options associated with a consumer handle.
 *
 * String members in @p out_options are borrowed from the handle and remain valid
 * until the handle is closed.
 *
 * @param consumer Consumer handle to inspect.
 * @param out_options Receives the borrowed option view.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_consumer_options(const xproc_c_consumer* consumer, xproc_c_options* out_options);

/**
 * @brief Returns the size of a retained pending message, if one exists.
 *
 * A pending message is stored when a prior call to xproc_c_consumer_poll_copy()
 * returned XPROC_C_STATUS_BUFFER_TOO_SMALL.
 *
 * @param consumer Consumer handle.
 * @param out_len Receives the retained message length in bytes.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_consumer_pending_len(const xproc_c_consumer* consumer, uint32_t* out_len);

/**
 * @brief Polls for the next consumer message and copies it into a caller buffer.
 *
 * When the buffer is too small, the message is retained internally so the caller
 * can retry with a larger buffer.
 *
 * @param consumer Consumer handle.
 * @param buffer Destination buffer. May be NULL only when @p buffer_capacity is zero.
 * @param buffer_capacity Size of @p buffer in bytes.
 * @param out_len Receives the message length in bytes.
 * @return XPROC_C_STATUS_OK when a message was copied, XPROC_C_STATUS_AGAIN when no message is available, or
 * XPROC_C_STATUS_BUFFER_TOO_SMALL when the message was retained for retry.
 */
XPROC_C_API xproc_c_status xproc_c_consumer_poll_copy(xproc_c_consumer* consumer, void* buffer,
                                                      uint32_t buffer_capacity, uint32_t* out_len);

/**
 * @brief Waits until the consumer may have new data available.
 *
 * If a retained pending message already exists, this returns immediately.
 *
 * @param consumer Consumer handle.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_consumer_wait(xproc_c_consumer* consumer);

/**
 * @brief Returns the socket port currently associated with a consumer handle.
 *
 * This is primarily useful for listener sockets that bind to port zero and receive
 * an OS-assigned port.
 *
 * @param consumer Consumer handle.
 * @param out_port Receives the current port.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_consumer_socket_port(const xproc_c_consumer* consumer, uint16_t* out_port);

/**
 * @brief Opens a read-only observer endpoint.
 *
 * @param options Observer options to use.
 * @param out_observer Receives the created handle on success. Set to NULL on failure.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_observer_open(const xproc_c_options* options, xproc_c_observer** out_observer);

/**
 * @brief Closes an observer handle.
 *
 * Passing NULL is allowed.
 *
 * @param observer Observer handle to destroy.
 */
XPROC_C_API void xproc_c_observer_close(xproc_c_observer* observer);

/**
 * @brief Returns the options associated with an observer handle.
 *
 * String members in @p out_options are borrowed from the handle and remain valid
 * until the handle is closed.
 *
 * @param observer Observer handle to inspect.
 * @param out_options Receives the borrowed option view.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_observer_options(const xproc_c_observer* observer, xproc_c_options* out_options);

/**
 * @brief Reads a point-in-time snapshot of ring state visible to an observer.
 *
 * @param observer Observer handle.
 * @param out_snapshot Receives the ring snapshot.
 * @return XPROC_C_STATUS_OK on success, otherwise an error status.
 */
XPROC_C_API xproc_c_status xproc_c_observer_snapshot(const xproc_c_observer* observer, xproc_c_snapshot* out_snapshot);

/**
 * @brief Peeks at the next visible message without consuming it and copies it into a caller buffer.
 *
 * @param observer Observer handle.
 * @param buffer Destination buffer. May be NULL only when @p buffer_capacity is zero.
 * @param buffer_capacity Size of @p buffer in bytes.
 * @param out_len Receives the visible message length in bytes.
 * @return XPROC_C_STATUS_OK when a message was copied, XPROC_C_STATUS_AGAIN when no message is visible, or
 * XPROC_C_STATUS_BUFFER_TOO_SMALL when the caller should retry with a larger buffer.
 */
XPROC_C_API xproc_c_status xproc_c_observer_peek_copy(xproc_c_observer* observer, void* buffer,
                                                      uint32_t buffer_capacity, uint32_t* out_len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XPROC_C_H_
