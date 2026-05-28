# Phase 2 Reference Design

Date: 2026-05-28
Branch target: `fix/binding-error`
Analysis baseline: `main`
Phase 1 closeout: `docs/superpowers/reviews/2026-04-25-channel-manifest-phase-1-closeout-status.md`

## Objective

Define the Phase 2 scope for the xproc C++ module. Phase 2 focuses on **runtime ergonomics**, **operational visibility**, and **socket resilience** -- building on the Phase 1 channel-manifest and builder foundation without expanding into new transport or concurrency models.

Phase 2 is organized into three priority tiers (P0--P3), each delivered as an independent spec and implementation branch.

## Guiding Principle

The next stage should improve **correctness under loosely-coupled integrations**, **runtime ergonomics**, and **operational visibility** before expanding into more ambitious transport or concurrency models.

## Priority Tiers

### P0: Runtime Allocation Improvement

**Spec:** [2026-05-28-phase2-runtime-allocation-design.md](../specs/2026-05-28-phase2-runtime-allocation-design.md)

`ipc::runtime` currently copies each message into a fresh `std::vector<uint8_t>` before dispatch. This is the single largest performance bottleneck for sustained high-throughput workloads.

Key deliverables:
- Buffer reuse across poll cycles
- Pluggable copy-policy hooks (zero-copy view vs eager copy vs small-buffer optimization)
- Optional message batching to amortize executor submission cost
- Backpressure signaling when consumer falls behind

### P1: Socket Disconnect / Reconnect Resilience

The socket backend (`socket_producer`, `socket_consumer`) has functional connect/accept flows but reconnect semantics are not hardened. This tier covers:

- Explicit stale-peer detection
- Clean re-entry into accept/connect after disconnect
- Decision on transparent vs caller-surfaced reconnect
- Improved `wait()` and `runtime::stop()` interruption behavior

### P1: Socket Test Coverage

Important gaps block confidence in socket transport correctness:

- Fixed-frame traffic over sockets
- Peer disconnect and reconnect behavior
- Socket runtime integration (`ipc::runtime` over `socket_consumer`)
- Dual-stack listen/connect edge cases

### P2: Observer / Inspector Diagnostic Helpers

`ring_snapshot` exposes raw atomic fields. The next layer should provide derived diagnostics:

- Ring occupancy ratio and byte-level usage
- Consumer lag in bytes (`write_pos - read_pos`)
- Producer liveness hints (commit progress since last snapshot)
- Time since last observed progress

### P2: C API Builder Parity

The C++ builder API (`make_fixed_channel`, `attach_fixed_channel`, etc.) has no C-level equivalent. This blocks builder ergonomics for Python, Node.js, and C# bindings.

Key deliverables:
- `xproc_c_make_fixed_channel(path, item_size, data_capacity)` returning pre-configured options
- `xproc_c_attach_fixed_channel(path)` inferring options from existing segment
- Equivalent helpers for variable-length channels

### P3: Codec Receive-Side Parity

`IByteCodec` has send-side helpers but lacks symmetric `poll_decoded` / `peek_decoded` on the receive side. The template codecs (`raw_pod_codec`, `bounded_bytes_codec`, `span_codec`) already have good APIs, so this is a gap-fill for the dynamic codec path.

### P3: Benchmark Expansion

Current benchmarks cover the SHM hot path well but lack visibility into:

- Socket vs SHM under equivalent workloads
- Observer overhead under load
- Runtime dispatch overhead
- Fixed vs variable-length workload trade-offs

## Phase 2 Exit Checklist

### P0: Runtime allocation

- [ ] Buffer reuse eliminates per-message allocation in the common path
- [ ] Copy-policy hooks allow callers to choose zero-copy, eager-copy, or small-buffer-optimized modes
- [ ] Batching mode amortizes executor submission cost
- [ ] Backpressure signal is documented and testable
- [ ] Existing `runtime` public API is either unchanged or migrated with a documented upgrade path

### P1: Socket resilience

- [ ] Stale peer sockets are detected and cleaned up
- [ ] Reconnect flow is tested for both producer (connect) and consumer (accept) sides
- [ ] Reconnect semantics (transparent vs surfaced) are documented
- [ ] `wait()` interruption is responsive, not polling-based

### P1: Socket test coverage

- [ ] Fixed-frame socket roundtrip tests exist
- [ ] Disconnect/reconnect tests exist for both sides
- [ ] `ipc::runtime` over `socket_consumer` is tested
- [ ] Dual-stack edge cases are covered on Linux and macOS

### P2: Observer diagnostics

- [ ] `occupancy_ratio()` and `occupancy_bytes()` helpers exist
- [ ] `consumer_lag_bytes()` helper exists
- [ ] `producer_liveness()` helper exists
- [ ] Helpers are exposed through C API and at least one binding

### P2: C API builder parity

- [ ] `xproc_c_make_fixed_channel` and `xproc_c_attach_fixed_channel` exist
- [ ] `xproc_c_make_varlen_channel` and `xproc_c_attach_varlen_channel` exist
- [ ] Python binding smoke test uses C API builders

### P3: Codec receive-side

- [ ] `poll_decoded(codec, handler)` exists on `consumer`
- [ ] `peek_decoded(codec, handler)` exists on `observer`
- [ ] Scratch-buffer helpers available for decode

### P3: Benchmarks

- [ ] Socket vs SHM benchmark for fixed and varlen workloads
- [ ] Observer overhead benchmark under sustained load
- [ ] Runtime dispatch overhead benchmark

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

After this reference spec is committed, each tier spec is written and reviewed independently. The P0 spec (`2026-05-28-phase2-runtime-allocation-design.md`) is written first and is the immediate next action.
