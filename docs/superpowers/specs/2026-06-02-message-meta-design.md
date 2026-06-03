# Message Meta Header Design

Date: 2026-06-02
Branch target: `ai-superpowers`
Implementation target: `feat/message-meta`

## Objective

Add a per-message metadata header (`message_meta`) to every ring slot, enabling request-response correlation, per-message schema identification, submission timestamps, and behavior flags. This brings xproc's message model closer to io_uring's SQE/CQE design where every submission carries structured metadata.

Additionally, remove the `json_codec_stub.hpp` and `protobuf_stub.hpp` files — these are example-level wrappers that do not belong in the library's public headers.

## Problem Statement

Every message in xproc's ring buffer is a raw payload with no per-message metadata:

```
Fixed slot:  [status: u32] [payload]
Varlen slot: [status: u32] [length: u32] [payload]
```

The only metadata is at the channel level (`control_block`: `schema_id`, `creator_timestamp_ns`, `creator_flags`), set once at creation time. This makes it impossible to:

- Correlate a request with its response (no `user_data` or correlation ID)
- Mark a message as high-priority or compressed (no `flags`)
- Track when a specific message was submitted (no per-message timestamp)
- Mix different message types on one channel (no per-message `schema_id`)

io_uring solves this by giving every SQE a 64-byte header with `opcode`, `flags`, `user_data`, etc. xproc needs an equivalent per-message header.

## Recommended Direction

Add a mandatory 24-byte `message_meta` struct between the internal ring header and the payload in every slot. This is not optional — every message carries it. The existing send APIs remain (auto-fill default meta), and new overloads accept explicit meta. The poll/peek callback signatures are extended to include meta (breaking change).

Also delete `json_codec_stub.hpp` and `protobuf_stub.hpp` from the protocol module — they are reference examples, not library code.

## API Design

### message_meta struct

New header: `include/xproc/ipc/message_meta.hpp`

```cpp
#pragma once

#include <cstdint>

namespace xproc::ipc {

struct message_meta {
  std::uint64_t user_data{0};     // opaque application tag (correlation, tracing)
  std::uint64_t timestamp_ns{0};  // steady_clock submission time (auto-filled by producer)
  std::uint32_t schema_id{0};     // per-message type identifier (overrides channel-level)
  std::uint32_t flags{0};         // behavior modifiers
};

enum message_flags : std::uint32_t {
  flag_none          = 0,
  flag_priority_high = 1u << 0,
  flag_compressed    = 1u << 1,
  flag_response      = 1u << 2,   // marks this as a response (user_data correlates to request)
  flag_cancel        = 1u << 3,   // cancel the operation identified by user_data
  // bits 0-15: reserved for library use
  // bits 16-31: available for application use
};

} // namespace xproc::ipc
```

`sizeof(message_meta) == 24` (two u64 + two u32, no padding on 64-bit). Always present in every slot.

### Ring slot layout

```
Fixed slot:  [ring_status: u32] [message_meta: 24B] [payload: item_size bytes]
Varlen slot: [ring_status: u32] [length: u32] [message_meta: 24B] [payload: ...]
```

The `ring_snapshot` struct is unchanged — it reflects ring-level state, not per-message metadata.

### Producer send API changes

Existing send methods remain unchanged (backward compatible). They internally pass a default `message_meta{}` where `user_data=0`, `flags=0`, and `timestamp_ns` is auto-filled by `reserve()` with `steady_clock::now()`.

New overloads accept explicit meta:

```cpp
// channel.hpp
void send_fixed_sized(const void* data, std::uint32_t byte_length, const message_meta& meta);
void send_fixed_bytes(const void* data, std::uint32_t payload_len, const message_meta& meta);
template <typename T> void send_fixed(const T& data, const message_meta& meta);
void send_varlen(const void* data, std::uint32_t len, const message_meta& meta);
```

The existing no-meta overloads delegate to the meta overloads with `message_meta{}`.

`try_send_*` and `send_*_for` get the same treatment — new overloads with `message_meta` parameter.

`send_encoded` in `messaging.hpp`:

```cpp
template <typename Codec>
void send_encoded(channel& ch, const typename Codec::message_type& msg, const message_meta& meta);
```

Ring layer: `fixed_writer::reserve()` and `varlen_writer::reserve()` gain a `const message_meta&` parameter. They write the meta into the slot after the ring header and return the payload pointer. If `meta.timestamp_ns == 0`, `reserve()` auto-fills it with `steady_clock::now().time_since_epoch().count()`. If non-zero, the user's value is preserved.

### Consumer poll API changes

**Breaking change.** The `poll` callback signature gains a `const message_meta&` first parameter:

```cpp
// Before: bool poll(handler) where handler is void(void* payload, uint32_t len)
// After:  bool poll(handler) where handler is void(const message_meta& meta, void* payload, uint32_t len)
```

Applies to:
- `channel::poll`
- `consumer::poll`
- `consumer_channel_interface::poll`

`observer::peek`:

```cpp
// Before: bool peek(handler) where handler is void(const void* payload, uint32_t len)
// After:  bool peek(handler) where handler is void(const message_meta& meta, const void* payload, uint32_t len)
```

`runtime::run` dispatch callback:

```cpp
// Before: void(const void* payload, uint32_t len)
// After:  void(const message_meta& meta, const void* payload, uint32_t len)
```

Ring layer: `fixed_reader::poll()` and `varlen_reader::poll()` read `message_meta` from the slot (after ring header) before reading the payload, and pass both to the handler.

### Codec integration

`poll_decoded` in `messaging.hpp` — the decoded handler gains meta:

```cpp
// Before: handler(const typename Codec::message_type& msg)
// After:  handler(const message_meta& meta, const typename Codec::message_type& msg)
```

`send_encoded` with meta overload passes meta through to the underlying send.

### C API changes

New meta struct and send-with-meta function:

```c
// xproc_c.h
typedef struct xproc_c_message_meta {
  uint64_t user_data;
  uint64_t timestamp_ns;   // 0 = auto-fill with steady_clock
  uint32_t schema_id;
  uint32_t flags;
} xproc_c_message_meta;

XPROC_C_API xproc_c_status xproc_c_producer_send_fixed_sized_with_meta(
    xproc_c_producer* producer, const void* data, uint32_t byte_length,
    const xproc_c_message_meta* meta);
```

Existing `xproc_c_producer_send_*` functions remain (use default meta internally).

Poll callback signature change:

```c
// Before: typedef void (*xproc_c_poll_handler)(void* payload, uint32_t len, void* user_data);
// After:  typedef void (*xproc_c_poll_handler)(const xproc_c_message_meta* meta, void* payload, uint32_t len, void* user_data);
```

Observer peek-with-meta:

```c
XPROC_C_API xproc_c_status xproc_c_observer_peek_copy_with_meta(
    xproc_c_observer* observer, xproc_c_message_meta* out_meta,
    void* buffer, uint32_t buffer_capacity, uint32_t* out_len);
```

### Protocol module cleanup

Delete:
- `include/xproc/protocol/json_codec_stub.hpp`
- `include/xproc/protocol/protobuf_stub.hpp`

Remove their includes from `include/xproc/xproc.hpp`.

Retain:
- `codec_traits.hpp` — codec contract definition
- `codecs.hpp` — `raw_pod_codec`, `bounded_bytes_codec`, `span_codec`
- `protocol.hpp` — `IByteCodec` dynamic interface

## Non-Goals

- io_uring-style submission queue / completion queue separation (this spec adds per-message metadata, not a new queuing model)
- Priority-based scheduling (flags encode priority but the runtime does not reorder yet)
- Cancel implementation (flag_cancel is defined but actual cancellation logic is a future spec)
- Multi-consumer support (SPSC layout unchanged)
- Changing `ring_snapshot` or `control_block` (ring-level state unchanged)

## API Compatibility

| Component | Changes | Breaking? |
|-----------|---------|-----------|
| `message_meta` struct | New | No -- new |
| `message_flags` enum | New | No -- new |
| `channel::send_fixed*` / `send_varlen` | +meta overloads | No -- additive |
| `channel::poll` callback | `message_meta` added to signature | **Yes** |
| `consumer::poll` callback | `message_meta` added to signature | **Yes** |
| `observer::peek` callback | `message_meta` added to signature | **Yes** |
| `runtime::run` callback | `message_meta` added to signature | **Yes** |
| `poll_decoded` handler | `message_meta` added to signature | **Yes** |
| `send_encoded` | +meta overloads | No -- additive |
| `json_codec_stub.hpp` | Deleted | **Yes** (removed) |
| `protobuf_stub.hpp` | Deleted | **Yes** (removed) |
| `xproc_c.h` | +meta struct, +send-with-meta, poll callback change | **Yes** (poll) |
| `ring_snapshot` | Unchanged | No |
| `control_block` | Unchanged | No |

## Testing Strategy

### C++ unit tests (`tests/message_meta_test.cpp`)

- `MetaDefaultValues` — `message_meta{}` defaults: user_data=0, flags=0, timestamp=0
- `FixedSlotContainsMeta` — fixed send, poll receives non-zero timestamp
- `VarlenSlotContainsMeta` — varlen send, poll receives non-zero timestamp
- `MetaUserDataRoundtrip` — send with user_data=42, poll receives user_data=42
- `MetaFlagsRoundtrip` — send with flags, poll receives same flags
- `MetaSchemaIdRoundtrip` — send with schema_id, poll receives same schema_id
- `DefaultMetaHasTimestamp` — no-meta send, poll receives non-zero timestamp
- `DefaultMetaHasZeroUserData` — no-meta send, poll receives user_data=0, flags=0

### Integration tests

- `EncodedSendPollWithMeta` — `send_encoded` / `poll_decoded` pass meta correctly
- `RuntimeDispatchIncludesMeta` — `runtime::run` callback receives correct meta
- `ObserverPeekIncludesMeta` — `observer::peek` callback receives correct meta

### C API tests (in `capi_smoke_test.cpp`)

- `CSendWithMetaSmoke` — `xproc_c_producer_send_fixed_sized_with_meta` + poll receives correct meta
- `CPollMetaSmoke` — C poll callback receives `xproc_c_message_meta*` correctly
- `CPeekCopyWithMetaSmoke` — `xproc_c_observer_peek_copy_with_meta` returns correct meta

## Success Criteria

- All existing poll/send call sites compile after updating callback signatures
- Meta passes through the full send -> poll chain correctly
- No-meta send overloads auto-fill timestamp, user_data and flags default to 0
- `json_codec_stub.hpp` and `protobuf_stub.hpp` deleted, build passes
- All existing tests updated and passing with new callback signatures

## Evidence Sources

- `include/xproc/ringbuffer/detail/fixed_header.hpp` — current fixed slot header
- `include/xproc/ringbuffer/detail/varlen_header.hpp` — current varlen slot header
- `include/xproc/ipc/channel.hpp` — send/poll APIs
- `include/xproc/ipc/observer.hpp` — peek API
- `include/xproc/ipc/messaging.hpp` — send_encoded / poll_decoded
- `include/xproc/protocol/json_codec_stub.hpp` — to be deleted
- `include/xproc/protocol/protobuf_stub.hpp` — to be deleted
- `include/xproc/xproc.hpp` — umbrella header
- `capi/xproc_c.h` — C API
- `capi/xproc_c.cpp` — C API implementation
- `tests/protocol_codec_test.cpp` — codec tests to update

## Transition Rule

After this spec is reviewed and approved, the next step is to write the implementation plan using the `writing-plans` skill, targeting `feat/message-meta`.
