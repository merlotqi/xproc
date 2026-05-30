# Phase 2 Reference Design

Date: 2026-05-28
Branch target: `fix/binding-error`
Analysis baseline: `main`
Phase 1 closeout: `docs/superpowers/reviews/2026-04-25-channel-manifest-phase-1-closeout-status.md`

## Objective

Define the Phase 2 scope for the xproc C++ module. Phase 2 focuses on **producer-side send control**, **runtime ergonomics**, **operational visibility**, and **socket resilience** -- building on the Phase 1 channel-manifest and builder foundation without expanding into new transport or concurrency models.

Phase 2 is organized into four priority tiers (P0--P3), each delivered as an independent spec and implementation branch.

## Guiding Principle

The next stage should improve **correctness under loosely-coupled integrations**, **runtime ergonomics**, and **operational visibility** before expanding into more ambitious transport or concurrency models.

## Priority Tiers

### P0: Runtime Allocation Improvement -- DONE (2026-05-28)

**Spec:** [2026-05-28-phase2-runtime-allocation-design.md](../specs/2026-05-28-phase2-runtime-allocation-design.md)
**Plan:** [2026-05-28-phase2-runtime-allocation.md](../plans/2026-05-28-phase2-runtime-allocation.md)
**Branch:** `main` (commits `79de17a`..`0ced62b`)

`ipc::runtime` currently copies each message into a fresh `std::vector<uint8_t>` before dispatch. This is the single largest performance bottleneck for sustained high-throughput workloads.

Implemented:
- `copy_policy` enum: `reuse_buffer` (default), `zero_copy`, `sbo`
- Internal reusable buffer in `runtime` eliminates per-message allocation
- `run_batched()` for amortizing executor submission cost
- Backpressure-aware `run()` overload with `on_backpressure(size_t)` callback
- 9 unit tests covering all policies, batching, backpressure, interface path, varlen
- Runtime dispatch benchmark comparing baseline vs all three policies
- Updated `runtime_dispatch_demo` with thread-pool executor and sbo policy
- Fixed lost-wakeup race in `stop()` via `commit_seq.fetch_add(1)` before `notify_all`

### P0: Producer Backpressure -- DONE (2026-05-28)

**Spec:** [2026-05-28-producer-backpressure-design.md](../specs/2026-05-28-producer-backpressure-design.md)
**Plan:** [2026-05-28-producer-backpressure.md](../plans/2026-05-28-producer-backpressure.md)
**Branch:** `main` (commits `fbed8fc`..`3779092`)

The current `send_fixed*` and `send_varlen` APIs block indefinitely when the ring buffer is full. Producers need non-blocking and bounded-time send options for responsive applications.

Key deliverables:
- Non-blocking `try_send_*` APIs returning `send_result::{ok, full, message_too_large}`
- Bounded-time `send_*_for(timeout)` APIs returning `send_result::{ok, timeout}`
- Oversized-message fast-fail (no wasted CAS on messages larger than ring capacity)
- Ring occupancy watermarks: `used_bytes()`, `available_bytes()`, `fill_ratio()`, `capacity_bytes()`
- Fixed-channel slot stride fix (reserve `item_size` not `byte_length`)
- Existing blocking `send_fixed*` / `send_varlen` unchanged (backward compatible)

### P2: Socket Disconnect / Reconnect Resilience

**Spec:** [2026-05-29-socket-disconnect-reconnect-resilience-design.md](../specs/2026-05-29-socket-disconnect-reconnect-resilience-design.md)
**Plan:** [2026-05-29-socket-disconnect-reconnect-resilience.md](../plans/2026-05-29-socket-disconnect-reconnect-resilience.md)

The socket backend (`socket_producer`, `socket_consumer`) has functional connect/accept flows but reconnect semantics are not hardened. This tier covers:

- Explicit stale-peer detection
- Clean re-entry into accept/connect after disconnect
- Decision on transparent vs caller-surfaced reconnect
- Improved `wait()` and `runtime::stop()` interruption behavior

### P2: Socket Test Coverage

Important gaps block confidence in socket transport correctness:

- Fixed-frame traffic over sockets
- Peer disconnect and reconnect behavior
- Socket runtime integration (`ipc::runtime` over `socket_consumer`)
- Dual-stack listen/connect edge cases

### P3: Observer / Inspector Diagnostic Helpers

`ring_snapshot` exposes raw atomic fields. The next layer should provide derived diagnostics:

- Ring occupancy ratio and byte-level usage
- Consumer lag in bytes (`write_pos - read_pos`)
- Producer liveness hints (commit progress since last snapshot)
- Time since last observed progress

### P3: C API Builder Parity

The C++ builder API (`make_fixed_channel`, `attach_fixed_channel`, etc.) has no C-level equivalent. This blocks builder ergonomics for Python, Node.js, and C# bindings.

Key deliverables:
- `xproc_c_make_fixed_channel(path, item_size, data_capacity)` returning pre-configured options
- `xproc_c_attach_fixed_channel(path)` inferring options from existing segment
- Equivalent helpers for variable-length channels

### P4: Codec Receive-Side Parity

`IByteCodec` has send-side helpers but lacks symmetric `poll_decoded` / `peek_decoded` on the receive side. The template codecs (`raw_pod_codec`, `bounded_bytes_codec`, `span_codec`) already have good APIs, so this is a gap-fill for the dynamic codec path.

### P4: Benchmark Expansion

Current benchmarks cover the SHM hot path well but lack visibility into:

- Socket vs SHM under equivalent workloads
- Observer overhead under load
- Runtime dispatch overhead
- Fixed vs variable-length workload trade-offs

## Phase 2 Exit Checklist

### P0: Runtime allocation

- [x] Buffer reuse eliminates per-message allocation in the common path
- [x] Copy-policy hooks allow callers to choose zero-copy, eager-copy, or small-buffer-optimized modes
- [x] Batching mode amortizes executor submission cost
- [x] Backpressure signal is documented and testable
- [x] Existing `runtime` public API is either unchanged or migrated with a documented upgrade path

### P0: Producer backpressure

- [x] `try_send_fixed_sized` / `try_send_fixed_bytes` / `try_send_varlen` return immediately on full ring
- [x] `send_fixed_sized_for` / `send_fixed_bytes_for` / `send_varlen_for` respect timeout
- [x] Oversized messages fail immediately with `message_too_large` (no CAS)
- [x] `used_bytes()`, `available_bytes()`, `fill_ratio()`, `capacity_bytes()` exposed on producer
- [x] Fixed-channel slot stride reserves `item_size` not `byte_length`
- [x] Existing blocking sends unchanged

### P2: Socket resilience

- [ ] Stale peer sockets are detected and cleaned up
- [ ] Reconnect flow is tested for both producer (connect) and consumer (accept) sides
- [ ] Reconnect semantics (transparent vs surfaced) are documented
- [ ] `wait()` interruption is responsive, not polling-based

### P2: Socket test coverage

- [x] Fixed-frame socket roundtrip tests exist
- [x] Disconnect/reconnect tests exist for both sides
- [x] `ipc::runtime` over `socket_consumer` is tested
- [x] Dual-stack edge cases are covered on Linux and macOS

### P3: Observer diagnostics

- [ ] `occupancy_ratio()` and `occupancy_bytes()` helpers exist
- [ ] `consumer_lag_bytes()` helper exists
- [ ] `producer_liveness()` helper exists
- [ ] Helpers are exposed through C API and at least one binding

### P3: C API builder parity

- [ ] `xproc_c_make_fixed_channel` and `xproc_c_attach_fixed_channel` exist
- [ ] `xproc_c_make_varlen_channel` and `xproc_c_attach_varlen_channel` exist
- [ ] Python binding smoke test uses C API builders

### P4: Codec receive-side

- [ ] `poll_decoded(codec, handler)` exists on `consumer`
- [ ] `peek_decoded(codec, handler)` exists on `observer`
- [ ] Scratch-buffer helpers available for decode

### P4: Benchmarks

- [ ] Socket vs SHM benchmark for fixed and varlen workloads
- [ ] Observer overhead benchmark under sustained load
- [x] Runtime dispatch overhead benchmark

## Deliverables per Tier

Each priority tier produces:
- **Spec document** in `docs/superpowers/specs/` -- defines scope, objective, success criteria
- **Implementation plan** in `docs/superpowers/plans/` -- step-by-step tasks for agentic execution
- **Audit** (as needed) in `docs/superpowers/audits/` -- post-implementation review findings

## Branch Strategy

- Each tier is implemented on a dedicated branch from `main`
- Specs and plans are authored on `ai-superpowers` (this worktree)
- Implementation branches follow the pattern `feat/<tier-slug>`
- Phase 2 reference spec lives on `ai-superpowers` and is the index into all tier specs

## Transition Rule

P0 (runtime allocation) and **P0: Producer Backpressure** are complete and merged to `main`. The next tier to implement is **P2: Socket Disconnect/Reconnect Resilience**.
