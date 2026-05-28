# Contributing to xproc

Thanks for contributing.

## Development Setup

### Prerequisites

- C++17 compiler
- CMake 3.14+
- Ninja or Make
- Linux, macOS, or Windows

Optional:

- Docker (see `Dockerfile`)
- Google Benchmark (handled via FetchContent when benchmarks are enabled)

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For shared library builds:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXPROC_BUILD_SHARED=ON
cmake --build build --parallel
```

### Run Tests

```bash
ctest --test-dir build --output-on-failure
```

On **Windows**, parallel `ctest` (`-j` greater than 1) can run multiple executables that reuse similar shared-memory object names, or leave test binaries locked while another job links (LNK1168). Prefer a single job when debugging SHM-related failures:

```powershell
ctest --test-dir build -C Debug -j 1 --output-on-failure
```

See also **Shared Memory Path Guidance** below.

### Run Benchmarks

```bash
cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
cmake --build build --target xproc_run_benchmarks
```

## Code and PR Guidelines

- Keep changes focused and minimal.
- Add or update tests for behavior changes.
- Preserve Linux, macOS, and Windows compatibility.
- Update docs (`README.md`, `docs/*.rst`) when APIs or build flags change.
- Keep benchmark changes reproducible and avoid noisy environmental assumptions.

## Commit / PR Checklist

- [ ] Project configures successfully (`cmake -S . -B build`)
- [ ] Build passes (`cmake --build build`)
- [ ] Tests pass (`ctest --test-dir build --output-on-failure`)
- [ ] Documentation updated for user-facing changes
- [ ] Changelog updated for notable changes

## Roadmap / TODO

The current codebase already covers the core SPSC shared-memory path, read-only observation, template codecs, and a minimal TCP transport. The next implementation steps we should prioritize are:

- [x] Socket transport: add true IPv6 / dual-stack support. `transport_options::socket_host` advertises IPv4/6, but the current connect / listen code is effectively IPv4-only.
- [ ] Socket resilience: reset stale peer sockets after disconnects, support clean reconnect flows, and make socket `wait()` / `runtime::stop()` interruption more explicit than short sleep polling.
- [x] Transport validation: tighten `validate_transport_options()` for socket-specific invariants such as port requirements, listen/connect role expectations, and retry parameter bounds.
- [ ] Runtime ergonomics: add lower-allocation dispatch modes for `ipc::runtime` (for example batching, buffer reuse, or explicit copy-policy hooks) instead of always allocating a fresh `std::vector<uint8_t>` per message.
- [ ] Messaging parity: add receive-side helpers for `protocol::IByteCodec::unwrap(...)` so dynamic codecs have a symmetric API with the existing template `send_encoded` / `poll_decoded` helpers.
- [ ] Observer / inspector tooling: build higher-level lag, occupancy, and producer-liveness helpers on top of `ring_snapshot`, and document concurrent observer + consumer semantics more clearly.
- [ ] Test coverage: add cases for socket fixed-frame traffic, peer disconnect / reconnect, varlen observer peek, runtime over `consumer_channel_interface`, and Windows stale-name / mapping-collision scenarios.
- [ ] Benchmarks: measure socket vs shared-memory backends for fixed / varlen workloads, and quantify observer / runtime overhead under sustained load.
- [ ] Documentation and examples: add focused guides for `transport_factory`, socket setup, optional JSON / Protobuf codecs, and observer-driven diagnostics workflows.

## Shared Memory Path Guidance

Use unique `transport_options::path` values in tests and long-running integrations.
On Windows, `core::unlink` does not remove mapping names, so uniqueness helps avoid stale-name collisions.

## Reporting Issues

- Bug reports / feature requests: GitHub Issues
- Security reports: see `SECURITY.md`
