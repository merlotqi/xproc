# xproc Rust Binding Design

## Scope

The first version of the Rust binding is based on the existing `capi/xproc_c.h` and does not directly bind the C++ API. The deliverables are:

- A raw FFI crate: `rust/xproc-sys`
- A safe thin wrapper crate: `rust/xproc`
- Support for `Producer`, `Consumer`, and `Observer`
- Support for `Options`, error mapping, blocking `wait()`, `poll_copy()` / `peek_copy()`
- No async, no zero-copy, no codec layer abstraction

## Architecture

`xproc-sys` is responsible for declaring the C ABI, compiling or linking `xproc_c`, and exposing minimal `extern "C"` interfaces.

`xproc` is responsible for:

- Mapping `xproc_c_options` to Rust `Options`
- Mapping status and layout errors to `XprocError`
- Managing opaque handle open/close via RAII
- Immediately copying borrowed C strings at the FFI boundary to avoid lifetime leaks

## API Shape

The first version's public API shape:

- `Options`
- `Producer::open`, `send_fixed_bytes`, `send_fixed_sized`, `send_varlen`, `socket_port`
- `Consumer::open`, `options`, `pending_len`, `poll_copy`, `wait`, `socket_port`
- `Observer::open`, `options`, `snapshot`, `peek_copy`
- `validate_options_for`, `shm_size_for_data_capacity`, `shm_data_capacity_for_size`, `shm_unlink`

## Testing

First validate minimal happy paths using Rust integration tests:

- Fixed shared-memory producer/consumer roundtrip
- Varlen buffer-too-small followed by retry
- Observer snapshot / peek
- Invalid options surface a structured error

Tests are driven directly through the Rust API and reuse the existing local build artifacts of `xproc_c`.
