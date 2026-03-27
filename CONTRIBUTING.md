# Contributing to xproc

Thanks for contributing.

## Development Setup

### Prerequisites

- C++17 compiler
- CMake 3.14+
- Ninja or Make
- Linux or Windows

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

### Run Benchmarks

```bash
cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
cmake --build build --target xproc_run_benchmarks
```

## Code and PR Guidelines

- Keep changes focused and minimal.
- Add or update tests for behavior changes.
- Preserve Linux and Windows compatibility.
- Update docs (`README.md`, `docs/*.rst`) when APIs or build flags change.
- Keep benchmark changes reproducible and avoid noisy environmental assumptions.

## Commit / PR Checklist

- [ ] Project configures successfully (`cmake -S . -B build`)
- [ ] Build passes (`cmake --build build`)
- [ ] Tests pass (`ctest --test-dir build --output-on-failure`)
- [ ] API coverage gate remains clean when applicable
- [ ] Documentation updated for user-facing changes
- [ ] Changelog updated for notable changes

## Shared Memory Path Guidance

Use unique `transport_options::path` values in tests and long-running integrations.
On Windows, `shm::unlink` does not remove mapping names, so uniqueness helps avoid stale-name collisions.

## Reporting Issues

- Bug reports / feature requests: GitHub Issues
- Security reports: see `SECURITY.md`
