# Socket Builder API Design

Date: 2026-05-29
Branch target: `ai-superpowers`
Implementation target: `feat/socket-builder-api`
Related spec: [2026-05-29-socket-disconnect-reconnect-resilience-design.md](2026-05-29-socket-disconnect-reconnect-resilience-design.md)

## Objective

Add a C++ builder API for socket transports so users do not need to hand-author `transport_options` for common socket producer and consumer setup.

The shared-memory API already exposes fluent builders:

```cpp
auto channel = xproc::ipc::make_varlen_channel(path).create(32768);
auto producer = channel.open_producer();
auto consumer = xproc::ipc::attach_varlen_channel(path).open_consumer();
```

Socket users currently still need to fill low-level fields directly:

```cpp
xproc::ipc::transport_options opts;
opts.backend = xproc::ipc::transport_backend::socket;
opts.type = xproc::ipc::channel_type::varlen;
opts.socket_listen = true;
opts.socket_port = 0;
opts.socket_host.clear();
xproc::ipc::socket_consumer consumer(opts);
```

This is noisy, easy to get wrong, and inconsistent with the current C++ surface.

## Recommended Direction

Add socket-specific listen/connect builders that return socket-specific endpoint types.

Socket transport has backend-specific behavior that should remain visible:

- `socket_producer::reconnect()`
- `socket_producer::try_reconnect()`
- `socket_producer::is_connected()`
- `socket_consumer::is_connected()`

Returning generic `producer` / `consumer` as the primary path would hide these methods and make reconnect examples awkward. The builder should hide option plumbing without erasing socket-specific capabilities.

## API Design

Add a new header:

```cpp
#include <xproc/ipc/socket_builders.hpp>
```

Export it from `include/xproc/xproc.hpp`.

### Public Entry Points

```cpp
socket_varlen_listener_builder listen_varlen_socket();
socket_fixed_listener_builder listen_fixed_socket(std::uint32_t item_size);

socket_varlen_connector_builder connect_varlen_socket(std::string host, std::uint16_t port);
socket_fixed_connector_builder connect_fixed_socket(std::string host, std::uint16_t port,
                                                    std::uint32_t item_size);
```

Naming uses socket roles instead of shared-memory `create` / `attach` language:

- listen builders create `socket_consumer` endpoints
- connect builders create `socket_producer` endpoints

This keeps the API aligned with socket semantics.

### Listener Builders

Listener builders create socket consumer options.

Defaults:

- backend: `transport_backend::socket`
- role: `socket_listen = true`
- host: empty string, preserving current consumer bind behavior
- port: `0`, letting the OS assign a port
- channel type: fixed or varlen based on the entry point
- fixed item size: required for fixed channels

Methods:

```cpp
builder& with_port(std::uint16_t port);

transport_options options() const;
socket_consumer open_consumer() const;
```

Listener builders should not introduce bind-host behavior in this phase. The socket backend currently treats listen mode as binding according to its existing internal policy, and the builder should preserve that behavior.

### Connector Builders

Connector builders create socket producer options.

Defaults:

- backend: `transport_backend::socket`
- role: `socket_listen = false`
- host: constructor argument
- port: constructor argument
- connect retries: current `transport_options` default unless overridden
- connect retry delay: current `transport_options` default unless overridden
- channel type: fixed or varlen based on the entry point
- fixed item size: required for fixed channels

Methods:

```cpp
builder& with_connect_retries(int retries);
builder& with_connect_retry_ms(int retry_ms);

transport_options options() const;
socket_producer open_producer() const;
```

Validation remains centralized in the existing `validate_*_transport_options()` functions. Builders should construct `transport_options` and call the relevant validation path in `options()`.

### Example Usage

Variable-length reconnect:

```cpp
auto consumer = xproc::ipc::listen_varlen_socket().open_consumer();

auto producer = xproc::ipc::connect_varlen_socket("127.0.0.1", consumer.options().socket_port)
                    .with_connect_retries(50)
                    .with_connect_retry_ms(2)
                    .open_producer();

producer.send_varlen(payload.data(), static_cast<std::uint32_t>(payload.size()));
producer.reconnect();
```

Fixed-size loopback:

```cpp
auto consumer = xproc::ipc::listen_fixed_socket(sizeof(packet)).open_consumer();
auto producer = xproc::ipc::connect_fixed_socket("127.0.0.1", consumer.options().socket_port,
                                                 sizeof(packet))
                    .open_producer();
```

Low-level integration remains available:

```cpp
auto opts = xproc::ipc::connect_varlen_socket("127.0.0.1", port).options();
auto transport = xproc::ipc::create_producer_transport(opts);
```

## Data Flow

### Listener

```text
listen_varlen_socket()
  -> builder stores type=varlen, port=0, host=""
  -> options()
       -> transport_options{backend=socket, socket_listen=true, ...}
       -> validate_consumer_transport_options()
  -> open_consumer()
       -> socket_consumer(options())
       -> socket backend binds, updates options().socket_port
```

### Connector

```text
connect_varlen_socket("127.0.0.1", port)
  -> builder stores host/port and retry defaults
  -> options()
       -> transport_options{backend=socket, socket_listen=false, ...}
       -> validate_producer_transport_options()
  -> open_producer()
       -> socket_producer(options())
       -> existing connect retry loop
```

## Error Handling

The builder API should not introduce new socket behavior. It should surface the same validation and runtime errors as direct `transport_options` construction.

Expected errors:

- fixed socket builders with `item_size == 0` fail validation
- connect builders with an empty host fail validation
- connect builders with `port == 0` fail validation
- negative retry counts or retry delays fail validation
- `open_producer()` may throw existing socket connect errors
- `open_consumer()` may throw existing bind/listen errors

## Testing Strategy

Add focused C++ API surface tests.

Recommended coverage:

- `listen_varlen_socket().options()` sets socket backend, varlen type, listen mode, empty host, and port 0
- `listen_fixed_socket(item_size).options()` sets fixed type and item size
- `connect_varlen_socket(host, port).options()` sets socket backend, varlen type, connect mode, host, and port
- `connect_fixed_socket(host, port, item_size).options()` sets fixed type and item size
- connector retry overrides are reflected in options
- invalid fixed item size, empty connect host, zero connect port, and negative retry settings throw `std::invalid_argument`
- listener/connector `open_*()` complete a real loopback varlen roundtrip
- the socket reconnect example uses builders instead of helper functions that manually create `transport_options`

Existing socket transport tests remain the source of truth for disconnect/reconnect behavior. Builder tests only need enough integration coverage to prove that the builder surface opens real socket endpoints.

## Documentation And Examples

Update `examples/socket_varlen_reconnect_demo.cpp`:

- remove `make_socket_consumer_options()`
- remove `make_socket_producer_options()`
- construct endpoints with `listen_varlen_socket()` and `connect_varlen_socket(...)`

Update `examples/README.md` only if the usage description needs to mention the new builder API.

## Non-Goals

- no C API builder changes
- no Node, Python, Rust, or C# API changes
- no socket wire-format changes
- no change to reconnect semantics
- no new listener bind-host behavior
- no automatic producer reconnect inside `send_*`
- no observer support for socket backend
- no unification of shared-memory and socket builders into one larger abstraction
- no removal of direct `transport_options` construction

## Success Criteria

- [ ] C++ users can create socket consumers and producers without manually filling `transport_options`
- [ ] The primary builder path returns `socket_consumer` / `socket_producer`
- [ ] `socket_producer::reconnect()` remains directly usable from builder-created producers
- [ ] `.options()` remains available for factory/runtime integration
- [ ] Builder validation reuses existing `transport_options` validation rules
- [ ] The socket reconnect example uses the builder API
- [ ] Focused API surface tests cover options, validation, and a real loopback roundtrip

## Evidence Sources

- `include/xproc/ipc/shm_builders.hpp` -- existing shared-memory builder style
- `include/xproc/ipc/socket_channel.hpp` -- socket endpoint types and reconnect-specific API
- `include/xproc/ipc/options.hpp` -- transport option fields and validation
- `include/xproc/xproc.hpp` -- public umbrella include
- `examples/socket_varlen_reconnect_demo.cpp` -- current socket example with manual options helpers
- `tests/api_surface_test.cpp` -- current builder API surface tests
- `tests/socket_transport_test.cpp` -- socket behavior and reconnect tests

## Transition Rule

After this spec is reviewed and approved, the next step is to write an implementation plan targeting `feat/socket-builder-api`.
