# xproc TODO

This document summarizes the next practical work items for `xproc`.
It is intentionally focused on engineering priorities rather than a broad product vision.

## Guiding principle

The next stage should improve **correctness under loosely-coupled integrations**, **runtime ergonomics**,
and **operational visibility** before expanding into more ambitious transport or concurrency models.

## High priority

### 1. Add a self-describing channel manifest

Today, shared-memory attachers can infer the mapped segment size, but important protocol details are still configured
out-of-band. The shared control block should embed enough metadata for attach-time validation.

Suggested fields:

- Channel mode: fixed vs variable-length
- Fixed-channel `item_size`
- Data alignment
- Data capacity
- Optional application protocol version or schema hash
- Optional creator timestamp / flags

Expected outcome:

- Attachers no longer need to duplicate channel shape manually
- Misconfiguration becomes a clean validation failure instead of undefined behavior
- Cross-repository or cross-language integrations become safer

### 2. Provide higher-level creator / attacher builders

`transport_options` is still a low-level configuration surface. It would help to add small construction helpers such as:

- `make_fixed_channel(path, item_size).create(data_capacity)`
- `attach_fixed_channel(path)`
- `make_varlen_channel(path).create(data_capacity)`
- `attach_varlen_channel(path)`

Expected outcome:

- Fewer invalid combinations of `shm_size`, `item_size`, `channel_type`, and `create_if_missing`
- Easier onboarding for users who just want a working SHM channel

### 3. Expand validation and mismatch coverage

The project should add dedicated tests for attach-time mismatches:

- Fixed-channel `item_size` mismatch
- `channel_type` mismatch across endpoints
- `data_align` mismatch
- Future schema / manifest version mismatch
- Observer attach behavior against mismatched layouts

### Phase 1 exit checklist

For planning purposes, **Phase 1** means the first three items above:

1. Channel manifest + attach validation
2. Higher-level SHM builders
3. Mismatch coverage

Phase 1 should only be marked done when every remaining unchecked item below is closed.

#### Manifest / attach validation

- [x] The shared control block stores enough manifest metadata to distinguish fixed vs varlen layout, capacity,
      alignment, fixed-item sizing, and an application-level schema identifier.
- [x] Producer / consumer / observer attach paths fail with typed layout errors instead of silently accepting a
      mismatched SHM layout.
- [x] Default attach flows no longer require non-creators to duplicate channel shape manually in the common case.
- [x] Decision: creator timestamp / flags are explicitly deferred to a later phase and are not part of the
      Phase 1 / v1 manifest contract.
- [x] Refresh docs so the public attach contract is described in terms of manifest-backed validation rather than only
      low-level `transport_options` wiring.

#### Builder ergonomics

- [x] Add C++ builders for the happy-path SHM flows:
      `make_fixed_channel(path, item_size).create(data_capacity)`,
      `attach_fixed_channel(path)`,
      `make_varlen_channel(path).create(data_capacity)`,
      and `attach_varlen_channel(path)`.
- [x] Make the builder layer cover producer, consumer, and observer attach scenarios without requiring callers to
      hand-wire `shm_size`, `create_if_missing`, `channel_type`, and `item_size`.
- [x] Update at least one fixed-channel example and one varlen example to use the builder API.
- [x] Decision: builder parity for C / Node / Python is intentionally deferred to a dedicated bindings-parity stage
      and is not a Phase 1 exit criterion.

#### Validation / mismatch coverage

- [x] There is regression coverage for basic layout validation failures such as version mismatch and
      `channel_type` mismatch.
- [x] There is binding-level mismatch coverage for at least some manifest failures (`layout_type`,
      `fixed_item_size`, `schema_id`) so cross-language attach failures are observable.
- [x] Add a dedicated C++ attach-time test for fixed-channel `item_size` mismatch.
- [x] Add a dedicated C++ attach-time test for `data_align` mismatch.
- [x] Add dedicated C++ observer mismatch tests for wrong layout type and schema / manifest-version mismatch.
- [x] Decision: future manifest-version incompatibility will use a separate manifest-version concept in a later design
      stage rather than extending the Phase 1 layout version contract.

#### Done gate

- [x] README, examples, and user-facing docs reflect the final Phase 1 attach / builder workflow.
- [x] A targeted Phase 1 regression suite is easy to run locally and in CI.
- [ ] Phase 2 follow-up work is scoped behind dedicated design and implementation plans rather than being pulled back
      into the Phase 1 closeout milestone.

## Medium priority

### 4. Improve `ipc::runtime` allocation behavior

`ipc::runtime` currently copies each message into a fresh `std::vector<uint8_t>` before dispatch.
That is simple and safe, but it is not ideal for sustained high-throughput workloads.

Potential improvements:

- Buffer reuse
- Batching
- Explicit copy-policy hooks
- Small-buffer optimization
- Bounded queue / backpressure integration

### 5. Add receive-side parity for dynamic codecs

`IByteCodec` already has send-side helpers, but the receive side does not yet provide equally ergonomic unwrap helpers.

Suggested additions:

- `poll_decoded(codec, ...)`
- `peek_decoded(codec, ...)`
- Shared scratch-buffer helpers for unwrap

### 6. Build richer observer / inspector helpers

`ring_snapshot` is intentionally minimal. The next layer should expose derived diagnostics:

- Ring occupancy ratio
- Used / free bytes
- Consumer lag in bytes
- Producer liveness hints
- Time since last observed progress

This would make `observer` more useful for dashboards, debugging, and integration health checks.

## Socket transport work

### 7. Add disconnect / reconnect resilience

The socket backend is functional, but reconnect semantics still need to be clarified and hardened.

Work items:

- Detect stale peer sockets explicitly
- Re-enter accept / connect flow cleanly after disconnect
- Decide whether reconnect is transparent or surfaced to the caller
- Improve `wait()` and `runtime::stop()` interruption behavior

### 8. Add stronger socket test coverage

Important gaps remain:

- Fixed-frame socket traffic scenarios
- Peer disconnect and reconnect behavior
- Socket runtime integration
- Multi-platform edge cases for dual-stack listen / connect

## Performance and benchmark work

### 9. Extend benchmarking beyond SHM hot path

Current benchmarks cover shared-memory behavior well enough to guide low-level work, but there is still little visibility into:

- Socket vs SHM performance under equivalent workloads
- Observer overhead under load
- Runtime dispatch overhead
- Fixed vs variable-length workload trade-offs

## Bindings and API parity

### 10. Bring binding ergonomics closer to C++ parity

The C++ core still has the most ergonomic builder / codec surface, but the binding layers now cover the core transport
and manifest features.

Suggested steps:

- Add higher-level C API helpers that mirror `make_fixed_channel`, `make_varlen_channel`, and attach flows without
      requiring callers to fill every `xproc_c_options` field manually
- Mirror more builder-style ergonomics in Python and C#
- Add binding-level examples and smoke tests

## Documentation

### 11. Improve task-oriented documentation

The documentation is already solid on architecture and platform notes, but users would benefit from a few more focused guides:

- Shared-memory creator / attacher patterns
- Socket setup and reconnect expectations
- Observer-driven diagnostics workflows
- Optional JSON / Protobuf codec usage
- Channel sizing guidance for fixed and variable-length traffic

## Long-term ideas

These are worth tracking, but should not outrank the work above:

- Additional transport backends
- More advanced flow-control policies
- Stronger schema negotiation for cross-language messaging
- Expanded operational tooling

## Suggested implementation order

1. Add channel manifest + attach validation
2. Add higher-level builders
3. Add mismatch tests
4. Improve `ipc::runtime` allocation strategy
5. Add observer diagnostics helpers
6. Harden socket reconnect behavior
7. Improve bindings and task-oriented docs
