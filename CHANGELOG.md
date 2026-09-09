# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.2.0] - 2026-09-09

### Added

- `spscring` as a third-party submodule for the shared-memory SPSC ring, wait/notify, and layout ABI.
- `shm_channel` producer and consumer wrappers over the shared-memory transport.
- C API `xproc_c_message_meta` plus send/peek helpers that accept or return per-message metadata.
- Timed `reserve_for` on ring writers, with `send_*_for` delegating to that path.

### Changed

- In-tree `include/xproc/ringbuffer` and `include/xproc/sync` implementations are replaced by `spscring`.
- Poll/peek callbacks and send APIs take `message_meta`. Variable-length slots carry it on the wire; **fixed-size slots remain payload-only**, so `send_fixed_*_with_meta` / `xproc_c_producer_send_fixed_sized_with_meta` keep the argument for compatibility and readers still see default (zero) metadata.
- Optional JSON and Protobuf codec integrations and related CMake options are removed.

### Fixed

- Linux `atomic_wait` / `atomic_notify` now use process-shared `FUTEX_WAIT` / `FUTEX_WAKE`, so cross-process shared-memory waiters wake instead of blocking indefinitely.
- Windows CI generator and CMake policy settings for Visual Studio 2022 / 2026.

## [1.1.0] - 2026-05-21

### Added

- Shared-library build option (`XPROC_BUILD_SHARED`) and install updates.
- pkg-config generation/install support (`xproc.pc`).
- Docker-based CI workflow.
- Benchmark target split with `xproc_run_benchmarks`.
- Sphinx-compatible RST documentation under `docs/`.
- Project support files (`SECURITY.md`, `CONTRIBUTING.md`, `CODEOWNERS`).
- Initial `capi/` C API wrapper with opaque handles, byte-copy send/receive APIs, structured layout
  errors, option validation, and smoke tests.

### Changed

- Benchmark layout and execution model for better suite-level control.
- Transport validation now enforces socket connect-mode port requirements, retry bounds, and
  role-specific producer / consumer / observer expectations more explicitly.
- Socket transport now resolves IPv4 / IPv6 peers via `getaddrinfo(AF_UNSPEC)` and prefers a
  dual-stack IPv6 listener with IPv4 fallback.

### Fixed

- Benchmark crash paths related to invalid benchmark argument usage and ringbuffer benchmark stability.

## [0.2.0] - 2026-03-26

### Added

- Core xproc IPC/ringbuffer APIs, tests, examples, and CI foundation.
