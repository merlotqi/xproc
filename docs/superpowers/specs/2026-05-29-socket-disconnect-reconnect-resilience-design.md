# Socket Disconnect / Reconnect Resilience Design

Date: 2026-05-29
Branch target: `ai-superpowers`
Phase 2 reference: [2026-05-28-phase2-reference-design.md](../reference/2026-05-28-phase2-reference-design.md)
Implementation target: `feat/socket-disconnect-reconnect-resilience`

## Objective

Harden the socket backend when peers disconnect, reconnect, or stop while a runtime loop is waiting.

The goal is to keep the current socket transport simple and reliable:

- preserve existing fixed and variable-length wire formats
- keep consumer-side reconnect transparent
- make producer-side recovery explicit
- make `ipc::runtime::stop()` responsive for socket-backed consumers
- document and test the disconnect semantics across the C++ and C surfaces

## Current Behavior

The socket backend currently supports basic connect, accept, send, and poll flows:

- `socket_producer` connects in its constructor and retries only during construction.
- `socket_producer::send_*` writes to the current socket and throws `std::runtime_error` when `send()` fails.
- `socket_consumer` keeps its listen socket open for the lifetime of the consumer.
- `socket_consumer::poll_impl()` closes the peer socket after a `recv()` failure, allowing a later `poll()` to accept a new producer.
- `socket_consumer::wait()` uses a short sleep when connected and a timed `select()` when waiting for the first peer.
- `ipc::runtime::stop()` wakes shared-memory consumers by notifying `commit_seq`, but interface-backed consumers can only stop after `iface_->wait()` returns.

This is enough for loopback traffic and a simple reconnect happy path, but it leaves the connection lifecycle implicit and relies on polling timeouts for socket runtime shutdown.

## Recommended Direction

Use explicit producer recovery and transparent consumer recovery.

Producer disconnect behavior should be caller-surfaced:

- `send_*` does not automatically reconnect or retry a payload.
- on a socket write failure, the producer closes the stale socket and reports the existing runtime error.
- callers recover by invoking an explicit reconnect API or by constructing a new producer.

Consumer disconnect behavior should remain transparent:

- the listen socket stays open
- stale peer sockets are closed on EOF, reset, malformed frame, or partial frame failure
- the next `poll()` or blocking wait may accept a new producer

This avoids hidden duplicate sends. A failed `send()` may have written part of a frame before the OS reported failure, so automatic retry could duplicate or corrupt application-level messages.

## API Design

### Producer Reconnect

Add explicit reconnect helpers to `socket_producer`:

```cpp
bool is_connected() const noexcept;
void reconnect();
bool try_reconnect() noexcept;
```

Behavior:

- `is_connected()` returns whether the producer currently owns a connected socket handle.
- `reconnect()` closes any existing socket and runs the same bounded connect retry loop used by the constructor.
- `try_reconnect()` closes any existing socket, attempts the same reconnect operation, and returns `false` on failure without throwing.
- `send_*` methods keep their current signatures and throwing behavior.
- if `send_*` observes a socket write failure, the producer closes the stale socket before throwing.

The reconnect API is socket-specific in this phase. Shared-memory producers do not gain reconnect semantics.

### Consumer Connection State

Add lightweight connection-state helpers to `socket_consumer`:

```cpp
bool is_connected() const noexcept;
```

The state helper is advisory. It reflects whether a peer socket handle is currently present; it is not a liveness guarantee because TCP disconnects are discovered by I/O.

Consumer `poll()` remains the primary data path:

- no peer accepted: return `false`
- complete fixed or varlen frame: invoke handler and return `true`
- EOF/reset/partial frame/malformed varlen length: close the peer socket and return `false`
- later peer available on the listen socket: accept and resume polling

### Wait Interruption

Extend `consumer_channel_interface` with an optional interruption hook:

```cpp
virtual void interrupt_wait() noexcept {}
```

`ipc::runtime::stop()` calls `iface_->interrupt_wait()` for interface-backed consumers after setting `running_ = false`.

For `socket_consumer`, `wait()` should block on both socket readiness and an internal interrupt signal. The implementation should use the smallest platform-specific primitive that composes with `select()`:

- POSIX: a self-pipe or socketpair; `wait()` includes the read end in `select()`
- Windows: a local event socket or other select-compatible wake socket

When `interrupt_wait()` fires, `wait()` returns promptly. The runtime loop then observes `running_ == false` and exits. This removes the current dependency on 1 ms / 5 ms polling timeouts.

## Data Flow

### Producer Send Failure

```text
producer.send_varlen(payload)
  -> write prefix
  -> write payload
  -> send() fails
  -> close stale socket
  -> throw runtime_error

caller catches error
  -> producer.reconnect()
  -> producer.send_varlen(next_payload)
```

The failed payload is not retried by xproc. Callers that need application-level replay can add message IDs and retry after reconnect.

### Consumer Peer Replacement

```text
consumer.poll(handler)
  -> current peer readable
  -> recv() returns EOF/reset or a partial frame fails
  -> close peer socket
  -> return false

next consumer.poll(handler)
  -> listen socket has pending peer
  -> accept new peer
  -> read next complete frame
```

The consumer keeps one active peer at a time, matching the existing single-producer socket model.

### Runtime Stop

```text
runtime thread:
  poll() returns false
  iface_->wait() blocks on socket readiness + interrupt signal

control thread:
  runtime.stop()
    -> running_ = false
    -> iface_->interrupt_wait()

runtime thread:
  wait() returns
  loop exits
```

## Error Handling

Socket send errors remain runtime errors on throwing APIs. The implementation should improve state cleanup, not hide the failure.

Recommended internal handling:

- wrap low-level send failures in a socket transport runtime error message
- close the producer socket before propagating the error
- avoid reconnecting automatically from inside `send_*`
- keep constructor and `reconnect()` retry behavior consistent
- make `try_reconnect()` exception-free by catching connect failures and returning `false`

Consumer read errors are not surfaced from `poll()` unless they represent local programming errors. Peer disconnects, resets, partial frames, and malformed oversized varlen frames should close the peer socket and return `false`.

C API behavior should follow the existing mapping:

- failed producer send returns `XPROC_C_STATUS_RUNTIME_ERROR`
- explicit reconnect helpers can be added in a later C API parity step if needed
- consumer wait interruption remains an internal runtime behavior unless a public C wait-cancel API is designed separately

## Testing Strategy

Add focused C++ socket tests first:

- fixed-frame loopback still passes
- varlen loopback still passes
- consumer accepts a new producer after the first producer closes
- consumer accepts a new producer after a partial fixed frame disconnect
- consumer accepts a new producer after a partial varlen frame disconnect
- producer send after peer close fails and marks the producer disconnected
- producer `reconnect()` restores the connection and allows later sends
- `try_reconnect()` returns `false` without throwing when no listener is available
- `ipc::runtime` over `socket_consumer` exits promptly after `stop()` while idle

Add C API smoke coverage where it protects binding behavior:

- socket producer send failure maps to `XPROC_C_STATUS_RUNTIME_ERROR`
- existing socket fixed roundtrip remains green

Node, Python, Rust, and C# bindings can rely on the C mapping for this phase. Binding-specific reconnect helpers should wait until the C API decides whether reconnect is part of the stable cross-language surface.

## Non-Goals

- automatic producer reconnect inside `send_*`
- automatic retry of failed payloads
- multi-producer socket fan-in
- changing the socket wire format
- changing shared-memory reconnect semantics
- adding application-level acknowledgements
- exposing a public wait-cancel API in C, Node, Python, Rust, or C#
- making `is_connected()` a strong peer liveness check

## Success Criteria

- [ ] Producer closes stale sockets after write failure
- [ ] Producer exposes explicit reconnect helpers in C++
- [ ] Existing `send_*` signatures and throwing behavior remain compatible
- [ ] Consumer closes stale peer sockets after EOF/reset/partial frame failure
- [ ] Consumer can accept a new peer after fixed and varlen disconnect scenarios
- [ ] `runtime::stop()` wakes socket/interface-backed runtime loops without relying on polling sleeps
- [ ] Focused socket tests cover reconnect, partial-frame disconnect, and runtime stop latency
- [ ] C API smoke coverage verifies send-failure error mapping

## Evidence Sources

- `src/ipc/socket_channel.cpp` -- current socket connect, accept, wait, send, and poll implementation
- `include/xproc/ipc/socket_channel.hpp` -- socket producer and consumer public C++ surface
- `include/xproc/ipc/channel_interface.hpp` -- polymorphic consumer wait contract
- `include/xproc/ipc/runtime.hpp` -- runtime stop and interface wait integration
- `tests/socket_transport_test.cpp` -- current socket loopback, reconnect, and runtime coverage
- `capi/xproc_c.cpp` -- current C API exception-to-status mapping

## Transition Rule

After this spec is reviewed and approved, the next step is to write the implementation plan using the `writing-plans` skill, targeting a new `feat/socket-disconnect-reconnect-resilience` branch from `main`.
