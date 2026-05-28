# Producer Backpressure And Send-Control Design

Date: 2026-05-28
Branch target: `ai-superpowers`
Implementation target: `feat/producer-backpressure`

## Objective

Improve producer-side behavior when the shared-memory ring buffer is full or close to full, without changing the existing reliable blocking send semantics.

The goal is to let applications choose the right behavior for their workload:

- block until space is available
- fail immediately when the ring is full
- wait up to a caller-supplied timeout
- observe ring pressure and apply application-level throttling or dropping

## Problem Statement

The current producer API exposes only blocking send operations:

```cpp
producer.send_fixed(...);
producer.send_fixed_bytes(...);
producer.send_fixed_sized(...);
producer.send_varlen(...);
```

Internally these calls reserve space through `fixed_writer::reserve()` or `varlen_writer::reserve()`. When the ring does not have enough free capacity, the writer waits on `read_wake_seq` until the consumer advances `read_pos`.

This is a safe default because it preserves messages. But it also means a fast producer can block indefinitely when:

- the consumer is slower than the producer
- the consumer is temporarily stalled
- the consumer process exits without draining the ring
- a message is larger than the ring can ever hold

The last case is especially important: if a single message plus its ring header is larger than `data_capacity`, waiting cannot fix the problem. The send path should fail fast instead of waiting forever.

## Recommended Direction

Keep the current blocking send API as the default reliable path, and add explicit send-control APIs beside it.

This design recommends four focused improvements:

1. oversized-message fast failure
2. non-blocking `try_send_*` APIs
3. timeout-based `send_*_for` APIs
4. producer-visible ring watermarks

This phase should not add automatic shared-memory resizing or producer-side drop-oldest behavior.

## Current Data Flow

For fixed channels:

```text
producer.send_fixed(...)
  -> channel::send_fixed_sized(...)
  -> fixed_writer::reserve(...)
  -> memcpy payload
  -> fixed_writer::commit(...)
```

For variable-length channels:

```text
producer.send_varlen(...)
  -> varlen_writer::reserve(...)
  -> memcpy payload
  -> varlen_writer::commit(...)
```

`reserve()` owns the full-ring behavior. It compares `write_pos`, `read_pos`, and `data_capacity`; when there is not enough free space, it performs backoff and eventually waits on `read_wake_seq`.

## API Design

### Send Result

Add a small status enum for APIs that can fail without throwing:

```cpp
enum class send_result {
  ok,
  full,
  timeout,
  message_too_large,
  invalid_argument
};
```

The exact enum name may live under `xproc::ipc` or `xproc::ringbuffer`; the public producer API should expose stable `xproc::ipc` semantics.

### Non-Blocking Sends

Add non-blocking producer APIs:

```cpp
bool try_send_fixed(const T& data);
send_result try_send_fixed_sized(const void* data, std::uint32_t byte_length);
send_result try_send_fixed_bytes(const void* data, std::uint32_t payload_len);
send_result try_send_varlen(const void* data, std::uint32_t len);
```

Behavior:

- return success when the message is reserved, copied, and committed
- return `full` when the ring currently lacks capacity
- return `message_too_large` when the message can never fit
- preserve existing throwing behavior for role/type misuse on the blocking APIs

The `bool try_send_fixed<T>()` convenience API can return `false` for any non-success status, while the lower-level sized APIs return `send_result` for diagnostics.

### Timeout Sends

Add bounded-wait producer APIs:

```cpp
template <typename Rep, typename Period>
send_result send_fixed_for(const T& data,
                           const std::chrono::duration<Rep, Period>& timeout);

template <typename Rep, typename Period>
send_result send_fixed_sized_for(const void* data,
                                 std::uint32_t byte_length,
                                 const std::chrono::duration<Rep, Period>& timeout);

template <typename Rep, typename Period>
send_result send_fixed_bytes_for(const void* data,
                                 std::uint32_t payload_len,
                                 const std::chrono::duration<Rep, Period>& timeout);

template <typename Rep, typename Period>
send_result send_varlen_for(const void* data,
                            std::uint32_t len,
                            const std::chrono::duration<Rep, Period>& timeout);
```

Behavior:

- wait for free capacity until the deadline expires
- return `timeout` if capacity does not become available in time
- return `message_too_large` immediately for impossible messages
- treat zero timeout as equivalent to `try_send_*`

The implementation can use repeated `try_reserve` plus bounded backoff. A later phase may add a true timed atomic wait abstraction if benchmarks show the polling/backoff path is too expensive.

### Ring Watermarks

Expose read-only producer/consumer inspection helpers:

```cpp
std::size_t capacity_bytes() const;
std::size_t used_bytes() const;
std::size_t available_bytes() const;
double fill_ratio() const;
```

These values are snapshots based on monotonic `write_pos - read_pos`. They are advisory, not synchronization guarantees. Applications can use them for throttling, monitoring, and pressure-aware dropping.

## Ringbuffer Layer Changes

Add reserve variants under `fixed_writer` and `varlen_writer`:

```cpp
reserve_result try_reserve(...);
reserve_result reserve_for(...);
```

The result should carry:

- status
- payload pointer on success
- logical position on success

The existing blocking `reserve()` remains and can be implemented in terms of the new lower-level helper, or it can keep its current hot path and share only the capacity checks.

### Oversized Detection

Before waiting, writers must check whether the aligned total message size exceeds `header_->data_capacity`.

For fixed channels:

```text
align_size(item_size + fixed_message_header) > data_capacity
```

For variable-length channels:

```text
align_size(len + varlen_message_header) > data_capacity
```

If true, return `message_too_large` or throw from the existing blocking API. Waiting on `read_wake_seq` is incorrect because no consumer progress can make the ring large enough.

### Fixed Channel Slot Stride

Audit `send_fixed_sized()` before implementation. The writer currently reserves based on the supplied `byte_length`, while the reader advances by configured `item_size`. If `byte_length < item_size`, writer and reader slot strides can diverge.

The fixed-channel send path should reserve the configured fixed slot size consistently. `send_fixed_bytes()` already does this. `send_fixed_sized()` should either:

- reserve `opts_.item_size` and copy `byte_length`, or
- be documented and renamed as a low-level exact-sized fixed-slot API only if the reader is also changed

This design recommends reserving `opts_.item_size` for all fixed-channel sends.

## Non-Goals

- automatic shared-memory ring expansion
- segment migration or live channel resizing
- producer-side mutation of `read_pos`
- drop-oldest semantics
- multi-producer support
- changing the existing blocking send API names

Automatic expansion requires a channel migration protocol: create a larger segment, notify the peer, drain or abandon the old segment, and handle failures during the switch. That is a separate design.

Drop-oldest is also out of scope. In the current SPSC layout, the consumer owns `read_pos`. Letting producer advance it to make room would risk racing the consumer and would blur ownership of unread data. Applications that prefer dropping should use `try_send_*` and drop the new message explicitly.

## Error Handling

Existing blocking APIs keep their current style:

- misuse such as wrong role or wrong channel type throws
- invalid message size throws or maps to a fast-fail helper internally
- full ring waits until space appears

New non-blocking and timeout APIs use `send_result` for expected flow-control outcomes:

- `full`
- `timeout`
- `message_too_large`

This separates normal backpressure from programming errors.

## Testing Strategy

Add focused C++ tests for:

- `try_send_*` succeeds when capacity is available
- `try_send_*` returns `full` when the ring is full
- `send_*_for` returns `timeout` when the consumer does not drain
- `send_*_for` succeeds when a consumer drains before the timeout
- oversized fixed and varlen messages fail immediately
- `send_fixed_sized()` uses the fixed channel slot stride consistently
- watermark helpers report expected capacity, used bytes, available bytes, and fill ratio

Existing blocking send tests should continue to pass unchanged.

## Benchmarking

Add a small benchmark or extend existing IPC benchmarks to compare:

- blocking send under no pressure
- `try_send` under no pressure
- `try_send` under full-ring pressure
- timeout send under pressure

The expected outcome is that `try_send` has minimal overhead versus blocking send in the no-pressure path, and returns quickly under full-ring pressure.

## Success Criteria

- Existing blocking producer APIs remain source-compatible.
- A producer can detect a full ring without blocking.
- A producer can wait for bounded time and recover on timeout.
- Impossible oversized sends fail immediately.
- Applications can observe ring occupancy without reading shared-memory internals.
- Fixed-channel slot stride is consistent for all fixed send variants.

## Evidence Sources

- `include/xproc/ipc/channel.hpp` -- current producer send APIs
- `include/xproc/ringbuffer/fixed_writer.hpp` -- fixed-channel reserve and full-ring wait behavior
- `include/xproc/ringbuffer/varlen_writer.hpp` -- variable-length reserve and wrap behavior
- `include/xproc/ringbuffer/fixed_reader.hpp` -- fixed-channel read advancement and producer wakeup
- `include/xproc/ringbuffer/varlen_reader.hpp` -- variable-length read advancement and dummy-slot handling
- `include/xproc/core/shm_layout.hpp` -- ring metadata fields
- `include/xproc/sync/atomic_backoff.hpp` -- current spin/yield/wait strategy

## Transition Rule

After this spec is reviewed and approved, the next step is to write the implementation plan using the `writing-plans` skill, targeting `feat/producer-backpressure`.
