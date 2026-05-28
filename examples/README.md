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
./build/examples/xproc_mpmc_inprocess_bridge_demo
./build/examples/xproc_mpmc_worker_pool_demo
./build/examples/xproc_mpsc_fan_in_demo
./build/examples/xproc_mpsc_log_hub_demo
./build/examples/xproc_spmc_config_broadcast_demo
./build/examples/xproc_spmc_fan_out_demo
./build/examples/xproc_fixed_channel_inprocess
./build/examples/xproc_varlen_channel_inprocess
./build/examples/xproc_observer_peek_demo
./build/examples/xproc_codec_roundtrip_demo
./build/examples/xproc_runtime_dispatch_demo
./build/examples/xproc_parent_child_counter_monitor
./build/examples/xproc_parent_child_struct_monitor
./build/examples/xproc_parent_child_varlen_monitor
./build/examples/xproc_handshake_launcher_demo
./build/examples/xproc_cpp_python_handshake_progress --python python3
./build/examples/xproc_node_cpp_child_struct_writer --shm-path /xproc_demo_from_node
./build/examples/xproc_ipc_taskflow_runtime_demo
./build/examples/xproc_ipc_taskflow_pipeline_demo
```

## What each example shows

- `xproc_ping_pong`  
  Linux two-process demo (`fork`): parent sends a fixed counter stream, child validates sequence.

- `xproc_mpmc_inprocess_bridge_demo`  
  In-process MPMC queue followed by an SPSC shared-memory bridge; the xproc ring remains single-producer/single-consumer.

- `xproc_mpmc_worker_pool_demo`  
  Mutex-backed worker-pool queue with an optional xproc SPSC telemetry side channel.

- `xproc_mpsc_fan_in_demo`  
  Multiple producers fan messages into one consumer.

- `xproc_mpsc_log_hub_demo`  
  Multi-producer log aggregation into an in-process queue, then one hub thread writes the SPSC channel.

- `xproc_spmc_config_broadcast_demo`  
  Single producer broadcasts configuration updates to multiple consumers.

- `xproc_spmc_fan_out_demo`  
  Single producer fans a message stream out to multiple consumers.

- `xproc_fixed_channel_inprocess`  
  Two threads in one process, fixed-size messages, using `make_fixed_channel(...).create(...)` plus
  `attach_fixed_channel(...)`.

- `xproc_varlen_channel_inprocess`  
  Two threads in one process, variable-length payloads, using `make_varlen_channel(...).create(...)` plus
  `attach_varlen_channel(...)`.

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

- `xproc_cpp_python_handshake_progress`
  C++ parent launches a Python worker, completes a bidirectional xproc handshake
  over two varlen channels, then receives progress events plus a final `done`
  signal. The demo intentionally focuses only on identity validation and
  progress transmission.

- `xproc_node_cpp_child_struct_writer`
  Cross-language child writer intended to be launched by the Node demo in `node/examples/node_parent_cpp_child_struct_monitor.ts`; attaches as a fixed-channel producer and publishes struct payloads into an existing SHM segment.

- `xproc_handshake_launcher_demo`
  Launches a child process and coordinates startup with an explicit xproc handshake.

- `xproc_ipc_taskflow_runtime_demo`  
  Connects xproc message delivery with a TaskFlow runtime.

- `xproc_ipc_taskflow_pipeline_demo`  
  Demonstrates xproc-driven pipeline stages with TaskFlow.

## Notes

- Examples use unique-ish shared memory paths and call `core::unlink` at exit.
- On Windows, `core::unlink` is a no-op by design; use unique names if rerunning frequently.
- `xproc_ping_pong` and parent/child demos use POSIX `fork`, so they are Linux/macOS-oriented.
