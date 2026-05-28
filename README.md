# xproc

High-performance **single-producer single-consumer (SPSC)** IPC built around
shared-memory ring buffers, with support for both fixed-size frames and
variable-length messages.

**Linux**: POSIX shared memory + futex  
**macOS**: POSIX shared memory + Darwin wait primitives  
**Windows**: named file mapping + polling/backoff in `atomic_wait`

## Features

- Lock-free SPSC communication with low-latency shared-memory transport
- Fixed-length and variable-length message modes
- Read-only observer attach for snapshots and peeking
- Builder/attacher APIs with schema validation and platform-specific namespace options
- Stable C API for Node, Python, and C# bindings
- Optional JSON and Protobuf codec integration
- CMake package export and `pkg-config` support

## Quick Start

```cpp
#include <xproc/xproc.hpp>

int main() {
    const std::string path = "/my_ipc_channel";
    auto channel = xproc::ipc::make_fixed_channel(path, sizeof(std::uint32_t))
        .with_schema_id(1)
        .create(1024 * 1024);

    auto producer = channel.open_producer();
    auto consumer = xproc::ipc::attach_fixed_channel(path)
        .with_schema_id(1)
        .open_consumer();

    std::uint32_t value = 42;
    producer.send_fixed_bytes(reinterpret_cast<const std::byte*>(&value), sizeof(value));

    consumer.poll([](void* data, std::uint32_t len) {
        (void)len;
        auto* received = static_cast<std::uint32_t*>(data);
        std::cout << *received << '\n';
    });
}
```

For C-facing consumers and language bindings:

```c
#include <xproc_c.h>
```

## Build and Install

- Build from source: [BUILD.md](BUILD.md)
- Install layout and downstream usage: [INSTALL.md](INSTALL.md)
- C# binding notes: [csharp/README.md](csharp/README.md)
- Node binding notes: [node/README.md](node/README.md)
- Python binding sources: [Python/](Python/)

## Documentation

- Sphinx docs: [docs/](docs/)
- Platform notes: [docs/platforms.rst](docs/platforms.rst)
- Design notes: [docs/design.md](docs/design.md)
- Examples: [examples/README.md](examples/README.md)
