# @merlotqi/xproc

`@merlotqi/xproc` is a Node.js binding for `xproc`, a single-producer single-consumer
IPC library with shared-memory and socket transports.

The package keeps the raw `Producer` / `Consumer` / `Observer` constructors, and
also adds higher-level `xproc.shm` and `xproc.socket` helpers for the common paths.

## Install

```bash
npm install @merlotqi/xproc
```

This package is intended to install and load directly from bundled prebuilt
Node-API binaries on these platforms:

- Linux x64 glibc
- Linux arm64 glibc
- macOS x64
- macOS arm64
- Windows x64

## Shared-memory quick start

Fixed-size channel:

```ts
const xproc = require("@merlotqi/xproc");

const created = xproc.shm.createFixedChannel({
  path: "/demo-fixed",
  itemSize: 4,
  dataCapacity: 16384n,
  schemaId: 0x1234n,
});

const producer = created.openProducer();
const consumer = xproc.shm.attachFixedChannel({
  path: "/demo-fixed",
  schemaId: 0x1234n,
}).openConsumer();

producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));
const payload = consumer.pollCopy();
```

Variable-length channel:

```ts
const xproc = require("@merlotqi/xproc");

const created = xproc.shm.createVarlenChannel({
  path: "/demo-varlen",
  dataCapacity: 32768n,
});

const producer = created.openProducer();
const consumer = xproc.shm.attachVarlenChannel({
  path: "/demo-varlen",
}).openConsumer();

producer.sendVarlen("hello");
const payload = consumer.pollCopy();
```

## Socket quick start

Fixed-size loopback:

```ts
const xproc = require("@merlotqi/xproc");

const listener = xproc.socket.listenFixed({
  host: "127.0.0.1",
  port: 0,
  itemSize: 4,
});

const consumer = listener.openConsumer();
const producer = xproc.socket.connectFixed({
  host: "127.0.0.1",
  port: consumer.socketPort(),
  itemSize: 4,
}).openProducer();

producer.sendFixedSized(Buffer.from([9, 8, 7, 6]));
const payload = consumer.pollCopy();
```

Variable-length loopback:

```ts
const xproc = require("@merlotqi/xproc");

const listener = xproc.socket.listenVarlen({
  host: "127.0.0.1",
  port: 0,
});

const consumer = listener.openConsumer();
const producer = xproc.socket.connectVarlen({
  host: "127.0.0.1",
  port: consumer.socketPort(),
}).openProducer();

producer.sendVarlen("hello-socket");
const payload = consumer.pollCopy();
```

## Runtime semantics

- `consumer.wait()` is a low-level synchronous wait and blocks the calling JavaScript thread. Use `await consumer.waitAsync()` when the wait may happen on the Node main thread.
- `consumer.pollCopy()` and `observer.peekCopy()` return newly allocated payload copies. Use `pollCopyInto(buffer)` or `peekCopyInto(buffer)` when you want to reuse caller-owned buffers and avoid the extra allocation.
- `Observer` is supported for shared-memory channels. Socket transports expose producer / consumer endpoints only.

## Raw API

The lower-level constructors are still available when you want direct control
over transport options:

```ts
const xproc = require("@merlotqi/xproc");

const producer = new xproc.Producer({
  path: "/demo-raw",
  shmSize: xproc.shmSizeForDataCapacity(16384n),
  channelType: "fixed",
  itemSize: 4,
  createIfMissing: true,
});
```

## Repository examples

When working in this repository, these example scripts exercise the high-level
Node API:

```bash
cd node
npm run example:fixed-channel-inprocess
npm run example:varlen-channel-inprocess
npm run example:observer-peek-demo
npm run example:socket-fixed-loopback
npm run example:socket-varlen-loopback
npm run example:parent-child-struct-monitor
```

Cross-language example:

```bash
cmake --build ../build --target xproc_node_cpp_child_struct_writer xproc_node
npm run example:node-parent-cpp-child-struct-monitor
```

## Development

Build from the repository root:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_NODE=ON
cmake --build build --target xproc_node
```

Install JavaScript dependencies once:

```bash
cd node
npm ci
```

Typecheck and test:

```bash
cd node
npm run typecheck
npm test
```
