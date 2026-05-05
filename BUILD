# Build xproc

This file covers building the project from source. For installation and downstream
consumption, see [INSTALL](INSTALL).

## Prerequisites

- C++17 compatible compiler
- CMake 3.14 or later
- On Linux: POSIX shared memory support
- On Windows: a 64-bit toolchain is recommended

Optional dependencies:

- `nlohmann_json` for JSON codec support
- `Protobuf` for Protocol Buffers support
- `dotnet` for the C# binding

## Basic Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

By default `xproc` is built as a static library. To build a shared library:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXPROC_BUILD_SHARED=ON
cmake --build build
```

## Common CMake Options

```bash
-DXPROC_BUILD_SHARED=ON
-DXPROC_BUILD_CAPI=ON
-DXPROC_BUILD_CAPI_SHARED=ON
-DXPROC_BUILD_TESTS=ON
-DXPROC_BUILD_EXAMPLES=ON
-DXPROC_BUILD_BENCHMARKS=ON
-DXPROC_BUILD_NODE=ON
-DXPROC_BUILD_PYTHON=ON
-DXPROC_WITH_NLOHMANN_JSON=ON
-DXPROC_WITH_PROTOBUF=ON
```

## Optional Features

```bash
# JSON codec support
cmake -S . -B build -DXPROC_WITH_NLOHMANN_JSON=ON

# Protobuf codec support
cmake -S . -B build -DXPROC_WITH_PROTOBUF=ON

# Benchmarks
cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
```

## Tests

```bash
cmake -S . -B build -DXPROC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Or use the aggregate target:

```bash
cmake --build build --target xproc_run_tests
```

On Windows, serial test runs are often easier to debug:

```bash
ctest -C Debug -j 1 --output-on-failure
```

## Examples

```bash
cmake -S . -B build -DXPROC_BUILD_EXAMPLES=ON
cmake --build build
./build/examples/xproc_ping_pong
```

Notable examples:

- `xproc_ipc_taskflow_runtime_demo`
- `xproc_ipc_taskflow_pipeline_demo`

See [examples/README.md](examples/README.md) for more example-specific notes.

## Benchmarks

```bash
cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
cmake --build build --target xproc_run_benchmarks
```

To run a single benchmark suite:

```bash
cmake --build build --target xproc_bench_ipc --parallel
./build/benchmarks/xproc_bench_ipc
```

## C# Binding

Build the native C ABI as a shared library first, then build the managed project:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_CAPI_SHARED=ON
cmake --build build --target xproc_c --config Debug
dotnet build csharp/XprocSharp/XprocSharp.csproj
```

See [csharp/README.md](csharp/README.md) for runtime lookup details and examples.

## Docker

The repo root [Dockerfile](Dockerfile) provides a minimal Ubuntu-based development image:

```bash
docker build -t xproc:dev .
docker run --rm -it -v "$(pwd):/workspace" -w /workspace xproc:dev bash
```

Inside the container:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CI runs the same flow in [`.github/workflows/docker.yml`](.github/workflows/docker.yml).
