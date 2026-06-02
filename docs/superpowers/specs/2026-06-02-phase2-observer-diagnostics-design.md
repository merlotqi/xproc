# Phase 2 P3: Observer / Inspector Diagnostic Helpers Design

Date: 2026-06-02
Branch target: `ai-superpowers`
Implementation target: `feat/observer-diagnostics`
Phase 2 reference: [2026-05-28-phase2-reference-design.md](../reference/2026-05-28-phase2-reference-design.md)

## Objective

Add derived diagnostic helpers to the `observer` class so that monitoring and tooling code can query ring buffer health without manually computing values from raw snapshot fields.

The observer currently exposes `ring_snapshot` with six raw atomic fields. Callers must know the ring buffer layout and manually compute occupancy, lag, and liveness. This spec brings the observer to parity with `channel`/`producer`/`consumer`, which already expose `used_bytes()`, `fill_ratio()`, and `capacity_bytes()`.

## Problem Statement

The `observer` class provides only:

```cpp
ring_snapshot snapshot() const;   // raw atomic fields
bool peek(handler);               // read next message without advancing
```

A monitoring agent that wants to know "how full is the ring?" must:

1. Call `snapshot()` to get `write_pos` and `read_pos`
2. Access `header_->data_capacity` (or know it from options)
3. Compute `used = write_pos - read_pos`, clamped to capacity
4. Divide to get a ratio

This is error-prone and duplicates logic already present in `ringbuffer_view::used_bytes()` and `ringbuffer_view::fill_ratio()`.

For liveness detection ("is the producer still making progress?"), callers must store a previous snapshot and compare `commit_seq` across two calls. There is no helper for this pattern.

The `channel`/`producer`/`consumer` classes already expose `used_bytes()`, `available_bytes()`, `fill_ratio()`, and `capacity_bytes()`. The observer should provide equivalent ergonomics.

## Recommended Direction

Add diagnostics in two layers:

1. **Stateless methods on `observer`** -- `occupancy_ratio()`, `occupancy_bytes()`, `available_bytes()`, `consumer_lag_bytes()`. These mirror the existing `channel` watermark helpers and require no new types.

2. **A stateful `diagnostics_tracker` class** -- stores a previous snapshot, provides `producer_alive()` (commit_seq delta) and `idle_duration_ms()` (wall-clock time since last write/read progress). This is a separate lightweight class that callers opt into.

This approach keeps `ring_snapshot` as a plain POD struct, avoids a new header for free functions, and follows the existing pattern where `channel` exposes derived helpers directly.

## API Design

### Stateless Diagnostics on observer

Add four methods to the `observer` class:

```cpp
// include/xproc/ipc/observer.hpp

double        occupancy_ratio() const;    // used / capacity, range [0.0, 1.0]
std::uint64_t occupancy_bytes() const;    // write_pos - read_pos, clamped to capacity
std::uint64_t available_bytes() const;    // capacity - used_bytes
std::uint64_t consumer_lag_bytes() const; // bytes written but not yet consumed
```

Implementation pattern (all four follow the same structure):

```cpp
double observer::occupancy_ratio() const {
  if (!header_) return 0.0;
  auto cap = header_->data_capacity;
  if (cap == 0) return 0.0;
  auto wp = header_->rb_meta.write_pos.load(std::memory_order_acquire);
  auto rp = header_->rb_meta.read_pos.load(std::memory_order_acquire);
  auto used = wp - rp;
  if (used > cap) used = cap;
  return static_cast<double>(used) / static_cast<double>(cap);
}
```

`consumer_lag_bytes` computes the same value as `occupancy_bytes` (`write_pos - read_pos`, clamped) but carries distinct semantics: it measures how far behind the consumer is. The two methods may diverge in future multi-consumer or segmented layouts.

### Stateful diagnostics_tracker

New header: `include/xproc/ipc/diagnostics_tracker.hpp`

```cpp
namespace xproc::ipc {

class diagnostics_tracker {
public:
  explicit diagnostics_tracker(const ring_snapshot& initial,
                               std::uint64_t data_capacity);

  void update(const ring_snapshot& snap);

  bool producer_alive() const;           // commit_seq changed since previous update
  std::uint64_t idle_duration_ms() const; // ms since last write_pos or read_pos change

  const ring_snapshot& current() const noexcept;
  const ring_snapshot& previous() const noexcept;
  std::uint64_t data_capacity() const noexcept;

private:
  ring_snapshot prev_;
  ring_snapshot curr_;
  std::uint64_t data_capacity_;
  std::chrono::steady_clock::time_point last_progress_;
};

} // namespace xproc::ipc
```

Key behaviors:

- Constructor sets both `prev_` and `curr_` to the initial snapshot, and `last_progress_` to `steady_clock::now()`. On the first `update()` call, the initial snapshot becomes `prev_` and the new snapshot becomes `curr_`; the first meaningful comparison happens then.
- `update(snap)` stores the new snapshot as `curr_`, shifts the old `curr_` to `prev_`. If `write_pos` or `read_pos` changed, resets `last_progress_` to `steady_clock::now()`.
- `producer_alive()` returns `curr_.commit_seq != prev_.commit_seq`. Returns `false` before the first `update()` (since `prev_ == curr_`).
- `idle_duration_ms()` returns `duration_cast<milliseconds>(steady_clock::now() - last_progress_).count()`. After construction with no `update()` calls, this measures time since creation.
- Uses `steady_clock` (monotonic, not affected by system time adjustments).

### C API

Add observer-handle diagnostic functions to `xproc_c.h`:

```c
xproc_c_status xproc_c_observer_occupancy_ratio(xproc_c_observer* obs, double* out);
xproc_c_status xproc_c_observer_occupancy_bytes(xproc_c_observer* obs, uint64_t* out);
xproc_c_status xproc_c_observer_available_bytes(xproc_c_observer* obs, uint64_t* out);
xproc_c_status xproc_c_observer_consumer_lag_bytes(xproc_c_observer* obs, uint64_t* out);
```

Each function internally calls the corresponding C++ observer method and writes the result to `*out`. Returns `XPROC_C_STATUS_OK` on success, `XPROC_C_STATUS_INVALID_ARGUMENT` if `obs` or `out` is NULL.

Add opaque tracker handle:

```c
typedef struct xproc_c_diagnostics_tracker xproc_c_diagnostics_tracker;

xproc_c_status xproc_c_diagnostics_tracker_create(
    xproc_c_observer* obs, xproc_c_diagnostics_tracker** out);

xproc_c_status xproc_c_diagnostics_tracker_update(
    xproc_c_diagnostics_tracker* tracker);

xproc_c_status xproc_c_diagnostics_tracker_producer_alive(
    xproc_c_diagnostics_tracker* tracker, bool* out);

xproc_c_status xproc_c_diagnostics_tracker_idle_ms(
    xproc_c_diagnostics_tracker* tracker, uint64_t* out);

void xproc_c_diagnostics_tracker_destroy(
    xproc_c_diagnostics_tracker* tracker);
```

Implementation: `xproc_c_diagnostics_tracker` wraps `std::unique_ptr<xproc::ipc::diagnostics_tracker>`. `create` reads the observer's initial snapshot and `data_capacity` to construct the tracker. `update` calls `observer->impl->snapshot()` and passes it to the C++ tracker. `destroy` is NULL-safe.

## Non-Goals

- Adding diagnostics to `producer` or `consumer` (they already have `used_bytes`/`fill_ratio`)
- Free-standing diagnostic functions in a separate header (observer methods are sufficient)
- Node.js or Python binding exposure (deferred to a later phase)
- Socket transport diagnostics (socket channels do not use ring buffers)
- Automatic alerting or threshold-based callbacks
- Multi-consumer lag tracking (current layout is SPSC)

## API Compatibility

| Component | Changes | Breaking? |
|-----------|---------|-----------|
| `observer` class | +4 new public methods | No -- additive |
| `diagnostics_tracker` class | New class in new header | No -- new |
| `xproc_c.h` | +9 new functions, +1 new opaque type | No -- additive |
| `ring_snapshot` struct | Unchanged | No |
| `ring_inspector_interface` | Unchanged | No |
| `channel` / `producer` / `consumer` | Unchanged | No |

## Testing Strategy

### C++ unit tests (`tests/ipc_diagnostics_test.cpp`)

Stateless observer diagnostics:

- `OccupancyRatioEmptyRing` -- freshly created channel, no messages sent; ratio == 0.0
- `OccupancyRatioPartialFill` -- send N messages; ratio in expected range
- `OccupancyRatioFullRing` -- fill ring completely; ratio ≈ 1.0
- `OccupancyBytesEmptyRing` -- empty ring; occupancy_bytes == 0
- `OccupancyBytesAfterSend` -- after sending; occupancy_bytes == expected
- `AvailableBytesEmptyRing` -- empty ring; available_bytes == data_capacity
- `ConsumerLagBytesNoConsumer` -- no consumer attached; lag == occupancy_bytes
- `ConsumerLagBytesAfterConsume` -- consumer reads; lag decreases
- `ObserverDiagnosticsMatchManualCalc` -- observer methods match manual `write_pos - read_pos` computation

Stateful diagnostics_tracker:

- `TrackerProducerAliveOnCommit` -- send a message, update tracker; producer_alive == true
- `TrackerProducerIdleNoActivity` -- no new messages, update tracker twice; producer_alive == false
- `TrackerIdleDurationIncreases` -- sleep briefly between updates; idle_duration_ms increases
- `TrackerIdleDurationResetsOnProgress` -- send message after idle; idle_duration_ms resets to near-zero
- `TrackerMultipleUpdates` -- several updates; prev/curr roll correctly

### C API smoke tests (in `tests/capi_smoke_test.cpp`)

- `COccupancyRatioSmoke` -- C API value matches C++ observer value
- `COccupancyBytesSmoke` -- C API value matches C++ observer value
- `CAvailableBytesSmoke` -- C API value matches C++ observer value
- `CConsumerLagBytesSmoke` -- C API value matches C++ observer value
- `CTrackerCreateUpdateDestroy` -- lifecycle: create, update, query, destroy; no leaks
- `CTrackerProducerAlive` -- C API tracker producer_alive matches expected

## Evidence Sources

- `include/xproc/ipc/inspector.hpp` -- `ring_snapshot`, `ring_inspector_interface`
- `include/xproc/ipc/observer.hpp` -- `observer` class with `snapshot()`, `header()`
- `include/xproc/ipc/channel.hpp` -- existing `used_bytes()`, `fill_ratio()`, `capacity_bytes()` pattern
- `include/xproc/ringbuffer/ringbuffer_view.hpp` -- `used_bytes()`, `fill_ratio()` implementation
- `include/xproc/core/shm_layout.hpp` -- `control_block` with `data_capacity`, `rb_meta`
- `capi/xproc_c.h` -- existing C API observer functions
- `capi/xproc_c.cpp` -- C API implementation pattern
- `tests/ipc_observer_attach_test.cpp` -- existing observer test
- `tests/capi_smoke_test.cpp` -- existing C API smoke test pattern

## Transition Rule

After this spec is reviewed and approved, the next step is to write the implementation plan using the `writing-plans` skill, targeting `feat/observer-diagnostics`.
