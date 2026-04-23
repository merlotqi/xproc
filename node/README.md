# xproc Node Binding

This package exposes the `xproc` C API to Node.js through a native addon plus a
small JavaScript facade.

## Build

From the repository root:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_NODE=ON
cmake --build build --target xproc_node
```

Install JavaScript dependencies once:

```bash
cd node
npm ci
```

## Typecheck and Test

```bash
cd node
npm run typecheck
npm test
```

## Examples

Pure Node examples:

```bash
cd node
npm run example:fixed-channel-inprocess
npm run example:varlen-channel-inprocess
npm run example:observer-peek-demo
npm run example:parent-child-struct-monitor
```

Cross-language example (Node parent + C++ child):

```bash
cmake --build ../build --target xproc_node_cpp_child_struct_writer xproc_node
npm run example:node-parent-cpp-child-struct-monitor
```

## What each example shows

- `examples/fixed_channel_inprocess.ts`
  A single Node process opens a fixed-size producer/consumer pair, sends `int32`
  counters, and validates the receive sequence.

- `examples/varlen_channel_inprocess.ts`
  A single Node process opens a varlen producer/consumer pair and exchanges text
  messages.

- `examples/observer_peek_demo.ts`
  Demonstrates the read-only `Observer` API, `peekCopy()`, and `snapshot()`
  alongside a normal consumer.

- `examples/parent_child_struct_monitor.ts`
  Node parent creates the SHM segment as consumer, relaunches itself as a child
  producer, and monitors fixed-size struct payloads.

- `examples/node_parent_cpp_child_struct_monitor.ts`
  Node parent creates the consumer and spawns the C++ executable
  `xproc_node_cpp_child_struct_writer`, which attaches as producer and publishes
  struct payloads into the same SHM segment.
