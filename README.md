# xproc

High-performance **Single Producer Single Consumer (SPSC)** Inter-Process Communication (IPC) library using ring buffers. Supports both **fixed-length frames** and **variable-length messages**. 

**Linux**: POSIX shared memory + futex  
**Windows**: Named file mapping (`CreateFileMapping` / `MapViewOfFile`). Waiting uses **polling with backoff** in `atomic_wait` (not `WaitOnAddress`): each `MapViewOfFile` gets a different virtual address, so address-based wake primitives do not pair across producer/consumer views or across processes. See **Platform Support** below and [docs/platforms.rst](docs/platforms.rst).

## Features

- **Lock-free SPSC communication** with microsecond latency
- **Cross-platform** support (Linux & Windows only)
- **Two message modes**: Fixed-length and variable-length
- **Variable-length payloads** can avoid extra copies in the ring (payload pointer is valid for the duration of `poll` / `peek` callbacks only)
- **Observer** read-only attach (`ipc_observer`) for snapshots / `peek` without advancing `read_pos` (weak consistency if a consumer runs concurrently)
- **Multiple serialization formats**: Built-in codecs, optional JSON (nlohmann/json), optional Protocol Buffers
- **Errors**: `std::invalid_argument` / `std::logic_error` for misuse; `xproc::shm::layout_exception` (with `validate_error code()`) for layout failures; `xproc::ipc::codec_exception` (with `codec_error code()`) for `send_encoded` / `poll_decoded` failures; `shm::last_os_error()` after failed `shm::open()`
- **Cache-line aligned** control block to reduce false sharing

## Quick Start

### Basic Usage

```cpp
#include <xproc/xproc.hpp>

// Producer side
xproc::ipc::transport_options opts;
opts.path = "/my_ipc_channel";
opts.shm_size = 1024 * 1024;
opts.type = xproc::ipc::channel_type::fixed;
opts.create_if_missing = true;

opts.item_size = 256; // fixed slot size in bytes
xproc::ipc::producer producer(opts);

std::string message = "Hello, IPC!";
producer.send_fixed_bytes(reinterpret_cast<const std::byte *>(message.data()),
                          static_cast<std::uint32_t>(message.size()));

xproc::ipc::consumer consumer(opts);
consumer.poll([](void *data, std::uint32_t len) {
    std::string received(static_cast<const char *>(data), static_cast<std::size_t>(len));
    std::cout << "Received: " << received << std::endl;
});
```

### Variable-Length Messages

```cpp
// Using variable-length channel
opts.type = xproc::ipc::channel_type::varlen;

xproc::ipc::producer producer(opts);
xproc::ipc::consumer consumer(opts);

std::vector<std::byte> data(1024);
producer.send_varlen(data.data(), static_cast<std::uint32_t>(data.size()));

consumer.poll([](void *ptr, std::uint32_t len) {
    process_data(static_cast<std::byte *>(ptr), len);
});
```

### Using Codecs

```cpp
#include <xproc/protocol/codecs.hpp>

// Using built-in codecs
xproc::ipc::producer producer(opts);
xproc::ipc::consumer consumer(opts);

// Send with codec
xproc::ipc::send_encoded<xproc::protocol::raw_pod_codec<int>>(producer, 42);

// Receive with codec
xproc::ipc::poll_decoded<xproc::protocol::raw_pod_codec<int>>(
    consumer, [](int value) {
        std::cout << "Received: " << value << std::endl;
    }
);
```

## Building

### Prerequisites

- C++17 compatible compiler
- CMake 3.16 or later
- For optional features:
  - nlohmann/json for JSON codec
  - Protocol Buffers for protobuf codec

### Basic Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

By default the `xproc` target is a **static** library. For a **shared** library (`libxproc.so` / `xproc.dll`):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXPROC_BUILD_SHARED=ON
cmake --build build
```

### Install and pkg-config

After installing to a prefix (headers, library, CMake package, and `lib/pkgconfig/xproc.pc`):

```bash
cmake --install build --prefix /usr/local
```

Consume with pkg-config (point `PKG_CONFIG_PATH` at `$(prefix)/lib/pkgconfig` if needed):

```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
pkg-config --cflags --libs xproc
```

`Libs.private` lists `-pthread` and `-lrt` on Linux so `pkg-config --libs --static xproc` can link a **static** `libxproc.a`. If you enable optional JSON or Protobuf codecs, those dependencies are not added to `xproc.pc`; pull in their flags separately (or use `find_package(xproc)`).

### Docker

The repo root [`Dockerfile`](Dockerfile) provides a minimal Ubuntu 22.04 image with GCC, CMake, Ninja, and pkg-config. Build the image, mount the source tree, then configure as usual:

```bash
docker build -t xproc:dev .
docker run --rm -it -v "$(pwd):/workspace" -w /workspace xproc:dev bash
# inside the container:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CI runs the same flow in [`.github/workflows/docker.yml`](.github/workflows/docker.yml).

### Build with Optional Features

```bash
# With JSON support
cmake -S . -B build -DXPROC_WITH_NLOHMANN_JSON=ON

# With Protocol Buffers support
cmake -S . -B build -DXPROC_WITH_PROTOBUF=ON

# With both tests and examples
cmake -S . -B build -DXPROC_BUILD_TESTS=ON -DXPROC_BUILD_EXAMPLES=ON

# With benchmarks (Google Benchmark via FetchContent)
cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
```

### Running Tests

```bash
cd build
ctest
```

On **Windows**, prefer serial test runs when debugging shared-memory tests to avoid stray handles and name collisions:

```bash
ctest -C Debug -j 1 --output-on-failure
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for details.

### Running Examples

```bash
cd build
./examples/xproc_ping_pong
```

**`xproc_ipc_taskflow_runtime_demo`** ([`examples/ipc_taskflow_runtime_demo.cpp`](examples/ipc_taskflow_runtime_demo.cpp)) uses the embedded [TaskFlow](examples/extern/taskflow/) library (`TaskFlow::TaskFlow` via `examples/CMakeLists.txt`) to run `ipc_runtime` message handlers on `TaskManager`'s thread pool. The example waits for each pool task to finish before returning to the runtime poll loop so shutdown stays correct on Windows (where `atomic_wait` on `commit_seq` does not wake on `stop()` alone). It is built whenever examples are enabled; see TaskFlow's [README](examples/extern/taskflow/README.md) and [LICENSE](examples/extern/taskflow/LICENSE).

**`xproc_ipc_taskflow_pipeline_demo`** ([`examples/ipc_taskflow_pipeline_demo.cpp`](examples/ipc_taskflow_pipeline_demo.cpp)) sends a batch of fixed-slot messages and, for each one, runs three chained TaskFlow stages (decode / compute / finalize) with progress updates and a small CPU loop in the compute stage. `TaskManager` is started with at least two worker threads so nested `submit_task` + wait cannot deadlock. The same per-message `future` synchronization as the runtime demo applies to the outer `ipc_runtime` dispatch.

### Running Benchmarks

Each benchmark source is a separate executable under `build/benchmarks/` (`xproc_bench_ipc`, `xproc_bench_ringbuffer`, etc.). To build and run all of them in one step:

```bash
cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
cmake --build build --target xproc_run_benchmarks
```

Or run a single suite, for example:

```bash
cmake --build build --target xproc_bench_ipc --parallel
./build/benchmarks/xproc_bench_ipc
```

## Architecture Overview

### Core Components

```
include/xproc/
├── platform/               # Platform abstraction
├── shm/                    # Shared memory management
├── sync/                   # Synchronization primitives
├── ringbuffer/             # Ring buffer implementations
├── ipc/                    # IPC channel abstractions
├── protocol/               # Message encoding/decoding
└── xproc.hpp               # Main header
```

### Key Classes

#### Ring Buffer Layer
- **`fixed_writer`** / **`fixed_reader`**: For fixed-length messages
- **`varlen_writer`** / **`varlen_reader`**: For variable-length messages
- **`ringbuffer_view`**: Base class for buffer access
- **`IRingBuffer`**: Polymorphic interface for testing

#### IPC Layer
- **`endpoint`**: Establishes shared memory connection
- **`producer`** / **`consumer`**: Type-safe channel wrappers
- **`ipc_observer`**: Read-only monitoring without interfering
- **`ipc_messaging`**: High-level send/receive operations

#### Protocol Layer
- **`codec_traits`**: Template traits for custom codecs
- **`raw_pod_codec<T>`**: For plain old data types
- **`bounded_bytes_codec<N>`**: For fixed-size byte arrays
- **`span_codec<MaxN>`**: `std::basic_string_view<std::byte>` wire view; decode points into ring memory until the `poll_decoded` handler returns

### Memory Layout

The shared memory layout consists of:

1. **Control Block**: Metadata and synchronization primitives
2. **Ring Buffer Metadata**: Write/read positions, sequence numbers
3. **Data Region**: Actual message storage

Key synchronization fields:
- **`write_pos`** / **`read_pos`**: Monotonic logical byte offsets
- **`commit_seq`**: Incremented after each message commit (for consumer waiting)
- **`read_wake_seq`**: Incremented when read position advances (for producer waiting)

### Performance Characteristics

- **Lock-free design**: No mutexes, only atomic operations
- **Cache-line alignment**: Prevents false sharing
- **Two-phase commit**: Reserve space, then commit for atomicity
- **Efficient waiting**: Linux uses futex; Windows uses **spin / yield / sleep polling** in `atomic_wait` (see platform notes)

## Platform Support

### Linux
- **Shared Memory**: `shm_open` + `mmap`
- **Synchronization**: futex (`FUTEX_WAIT` / `FUTEX_WAKE`)
- **Requirements**: POSIX shared memory support

### Windows
- **Build**: With the Visual Studio generator, pass `-A x64` (or use the default set by this project’s CMake) so MSVC targets x64; otherwise SDK headers can fail with `C1189: No Target Architecture`. With Ninja + MSVC, use an **x64 Native Tools** developer prompt (or matching vcvars) so `cl` defines `_M_X64` / `_WIN64`.
- **Shared Memory**: `CreateFileMapping` + `MapViewOfFile` (full section mapped; `VirtualQuery` must report `RegionSize >= opts.shm_size`; smaller sections fail `open`)
- **Synchronization**: `atomic_wait` / `atomic_notify_*` use **polling with backoff** on Windows; `atomic_notify_*` is a no-op (waiters observe `commit_seq` / `read_wake_seq` via loads). Linux uses futex with real wake.
- **Requirements**: 64-bit MSVC (x64) recommended; see [docs/platforms.rst](docs/platforms.rst).
- **Naming**: Logical paths map to `<namespace>\\xproc_<hash>_…` (default namespace `Local`, optional `Global` via `transport_options::win32_object_namespace`). Use **unique path strings** (PID, random salt, session id) in tests and long-running services to avoid accidental name reuse; see [docs/design.md](docs/design.md).
- **`shm::unlink`**: No-op on Windows; do not rely on it to clear a name before reuse.

**Note**: Only Linux and Windows are supported. Other platforms fail CMake configuration with a clear error.

### Tests on Windows

The `xproc_win32_wait_shm_tests` target runs `tests/win32_wait_shm_test.cpp`, including a **child process** that waits on `commit_seq` and receives a message from the parent (see [docs/design.md](docs/design.md)).

## Advanced Usage

### Custom Codecs

```cpp
struct my_point {
    int x, y;
};

struct point_codec {
    using message_type = my_point;
    static constexpr size_t max_encoded_size() { return 8; }
    
    static bool encode(const my_point& src, uint8_t* dst, size_t dst_len, size_t& out_len) {
        if (dst_len < 8) return false;
        memcpy(dst, &src, sizeof(my_point));
        out_len = sizeof(my_point);
        return true;
    }
    
    static bool decode(const uint8_t* src, size_t src_len, my_point& dst) {
        if (src_len < sizeof(my_point)) return false;
        dst = *reinterpret_cast<const my_point*>(src);
        return true;
    }
};

// Use custom codec
xproc::ipc::send_encoded<point_codec>(channel, my_point{10, 20});
```

### Observer Pattern

```cpp
// Create observer (read-only)
xproc::ipc::ipc_observer observer(opts);

observer.peek([](const void *data, std::uint32_t len) {
    std::cout << "Monitoring: " << len << " bytes" << std::endl;
    (void)data;
});
```

### Runtime Management (`ipc_runtime`)

`ipc_runtime` polls the consumer channel, **copies** each message into a `std::vector<uint8_t>`, then calls `pool_executor(lambda)` where `lambda` should eventually invoke `handler(uint8_t*, size_t)` (e.g. post `lambda` to your thread pool). See class comments in [`ipc_runtime.hpp`](include/xproc/ipc/ipc_runtime.hpp) for `Executor` contract, `stop()`, and exception behavior.

```cpp
xproc::ipc::consumer consumer(opts);
xproc::ipc::ipc_runtime rt(consumer);

auto pool = [](auto task) { task(); }; // replace with real thread pool post

rt.run(pool, [](const std::uint8_t *data, std::size_t len) {
    process_message(data, len);
});

rt.stop();
```

## Error Handling

- **`validate_transport_options`**: Central checks on `path`, `shm_size`, `item_size` (fixed), and `data_align` (`ipc_options.hpp`); used by `endpoint` / `ipc_observer`. On Windows, `win32_object_namespace` must be `Local` (default) or `Global`.
- **Layout**: `xproc::shm::layout_exception` derives from `std::runtime_error` and exposes `validate_error code()` for programmatic handling.
- **Codec / wire size**: `xproc::ipc::codec_exception` exposes `codec_error code()` for encode/decode failures and fixed-slot overflow in `send_encoded` / `poll_decoded` / `IByteCodec` overload.
- **Role misuse**: `std::logic_error` when calling `send_*` or `poll` on the wrong role.
- **SHM open failure**: `shm::last_os_error()` returns POSIX `errno` or Windows `GetLastError()` (as `int`) after a failed `open()`.

## Thread Safety

- **SPSC design**: Single producer, single consumer per channel
- **Lock-free operations**: Atomic operations ensure thread safety
- **Memory ordering**: Proper acquire/release semantics
- **No shared mutable state**: Between producer and consumer

## Performance Tips

1. **Use fixed-length messages** when possible for maximum performance
2. **Choose appropriate buffer sizes** to balance memory usage and throughput
3. **Avoid frequent small messages** to reduce synchronization overhead
4. **Use view-based codecs** only when you copy or process synchronously inside the poll handler
5. **Monitor buffer utilization** to tune performance

## Installing / `find_package`

After `cmake --install`, consumers use:

```cmake
find_package(xproc CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE xproc::xproc)
```

If xproc was built with `-DXPROC_WITH_NLOHMANN_JSON=ON` or `-DXPROC_WITH_PROTOBUF=ON`, the generated `xprocConfig.cmake` runs `find_dependency` for those packages so transitive headers link correctly.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Write tests for new functionality
4. Ensure all tests pass
5. Submit a pull request

**Shared memory paths in tests**: Prefer **unique** `transport_options::path` values (e.g. include process id or a random suffix). On Windows, `shm::unlink` does not remove a mapping name; unique paths avoid stale-segment collisions across CI runs.

## License

[License information goes here]

## See Also

- [Documentation (reStructuredText)](docs/) — Sphinx sources; build HTML with `sphinx-build -b html docs docs/_build/html` (see [docs/requirements.txt](docs/requirements.txt))
- [Examples](examples/) - Usage examples and demonstrations
- [Tests](tests/) - Comprehensive test suite
- [Protocol Buffer definitions](proto/) - Message format definitions
