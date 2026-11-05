# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- Upgraded the bundled `spscring` (0.2.0): ring commits and reads now wake the
  peer **only when a waiter is registered** on the notify word (waiter flag in
  bit 31 of `commit_seq` / `read_wake_seq`, in-segment `version_minor` 2).
  shm channel end-to-end benchmarks on Apple M2 improve ~17x (fixed 64B
  reserve/commit ~1.0 µs → ~55 ns per message). Diagnostic snapshots
  (`ring_snapshot`, `xproc_c_snapshot`) mask the waiter flag, so sequence
  counters keep their previous meaning.
- Blocking consumers (`shm_consumer::wait`, `ipc_runtime`, `runtime::stop`)
  register on the notify word while blocked via `spscring::wait_for_seq_change`;
  `stop()` wakes only registered waiters. Custom code that blocks directly on
  `atomic_wait(commit_seq, ...)` must migrate to `wait_for_seq_change`
  (or set the waiter bit) or it will miss wakes.
- `channel::send_varlen` failure messages now include the reserve status.

### Fixed

- `Meta.ConcurrentMultiWriterMultiReader` no longer aborts on transient ring
  backpressure: readers spawn before writers and writers retry with
  `send_varlen_for` inside a deadline instead of throwing on the first `full`
  reserve. (Concurrent multi-writer attach remains out of the SPSC contract.)

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
