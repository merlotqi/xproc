# xproc Examples

This folder contains runnable examples for common API usage patterns.

## Build

```bash
cmake -S .. -B ../build -DXPROC_BUILD_EXAMPLES=ON
cmake --build ../build --parallel
```

## Run

From repository root:

```bash
./build/examples/xproc_ping_pong
./build/examples/xproc_fixed_channel_inprocess
./build/examples/xproc_varlen_channel_inprocess
./build/examples/xproc_observer_peek_demo
./build/examples/xproc_codec_roundtrip_demo
./build/examples/xproc_runtime_dispatch_demo
./build/examples/xproc_parent_child_counter_monitor
./build/examples/xproc_parent_child_struct_monitor
./build/examples/xproc_parent_child_varlen_monitor
```

## What each example shows

- `xproc_ping_pong`  
  Linux two-process demo (`fork`): parent sends a fixed counter stream, child validates sequence.

- `xproc_fixed_channel_inprocess`  
  Two threads in one process, fixed-size messages (`producer` + `consumer`).

- `xproc_varlen_channel_inprocess`  
  Two threads in one process, variable-length payloads (`send_varlen` + `poll`).

- `xproc_observer_peek_demo`  
  Read-only observer (`ipc_observer::peek`) plus normal consumer drain, with ring snapshot print.

- `xproc_codec_roundtrip_demo`  
  Typed messaging helpers (`send_encoded` + `poll_decoded`) using `raw_pod_codec`.

- `xproc_runtime_dispatch_demo`  
  `ipc_runtime` run loop with a simple inline executor and copied payload handler.

- `xproc_parent_child_counter_monitor`  
  Parent creates IPC and forks child; child starts a writer thread that sends `0..100` every second; parent polls every 500ms and prints only changed values until child exits.

- `xproc_parent_child_struct_monitor`  
  Same parent/child monitor flow as above, but `send_fixed` transmits a struct payload: `char message[256]` + two `int` fields.

- `xproc_parent_child_varlen_monitor`  
  Same parent/child monitor flow as above, but `send_varlen` transmits variable-length text messages and the parent prints the payload size plus content when it changes.

## Notes

- Examples use unique-ish shared memory paths and call `shm::unlink` at exit.
- On Windows, `shm::unlink` is a no-op by design; use unique names if rerunning frequently.
- `xproc_ping_pong` uses `fork`, so it is Linux-oriented.
