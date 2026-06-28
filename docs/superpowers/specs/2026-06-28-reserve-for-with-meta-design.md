# reserve_for + message_meta: Restore Efficient Timed Reservation

**Date**: 2026-06-28
**Branch**: ai/superpowers
**Status**: draft

## Problem

`fixed_writer::reserve_for` and `varlen_writer::reserve_for` have two issues:

1. **API gap**: No `message_meta` parameter. The channel layer (`channel.hpp`) cannot call
   `reserve_for` directly with metadata, forcing it to inline a `try_reserve` + `yield`
   loop (with explicit TODO comments at line 195 and line 287).

2. **Degraded waiting**: The timed wait loop uses only `std::this_thread::yield()`,
   which burns CPU without backoff. In contrast, the blocking `reserve()` uses
   `atomic_backoff` with three-phase exponential spin → yield → `atomic_wait`.

### Current code paths

```
reserve()          → try_reserve + atomic_backoff (spin → yield → futex)  ✅ efficient
reserve_for()      → try_reserve + bare yield loop                        ❌ degraded
channel send_*_for → try_reserve + bare yield loop (inlined, no meta)     ❌ duplicated
```

## Design

### Approach

Add `message_meta` parameter to `reserve_for`, give it a three-phase backoff loop,
then simplify the channel layer to delegate directly.

### Phase 1: Ring buffer writers

Both `fixed_writer` and `varlen_writer` get:

1. A new overload: `reserve_for(size, timeout, meta)` — the main implementation
2. The existing `reserve_for(size, timeout)` becomes a convenience wrapper that
   delegates with empty `message_meta{}` (backward compatible)
3. The wait loop uses three-phase backoff matching `atomic_backoff` constants:

| Phase | Iterations | Behavior |
|-------|------------|----------|
| Spin  | 1–12       | Exponential CPU pause (1, 2, 4, ..., 256) |
| Yield | 13–22      | `std::this_thread::yield()` |
| Sleep | 23+        | `sleep_for(1ms)` or remaining deadline, whichever is smaller |

```cpp
// fixed_writer.hpp — new overload
template <typename Rep, typename Period>
reserve_result reserve_for(uint32_t item_size, const std::chrono::duration<Rep, Period>& timeout,
                           const xproc::ipc::message_meta& meta) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    uint32_t iteration = 0;
    while (true) {
        reserve_result rr = try_reserve(item_size, meta);
        if (rr.status != reserve_status::full) return rr;

        if (std::chrono::steady_clock::now() >= deadline)
            return {reserve_status::timeout, nullptr, 0};

        ++iteration;
        if (iteration <= 12) {
            const uint32_t exp = (std::min)(iteration - 1, 8u);
            const uint32_t delay = 1u << exp;
            for (uint32_t i = 0; i < delay; ++i) XPROC_CPU_PAUSE();
        } else if (iteration <= 22) {
            std::this_thread::yield();
        } else {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0)
                return {reserve_status::timeout, nullptr, 0};
            std::this_thread::sleep_for((std::min)(remaining, std::chrono::milliseconds(1)));
        }
    }
}
```

`varlen_writer` receives an identical change with `len` in place of `item_size`.

### Phase 2: Channel layer simplification

`channel.hpp` `send_fixed_sized_for` and `send_varlen_for` drop their inlined
`try_reserve` + `yield` loops and call `reserve_for(size, timeout, meta)` directly.
The TODO comments are removed. Public API signatures remain unchanged.

Before (23 lines of manual loop):
```cpp
const auto deadline = std::chrono::steady_clock::now() + timeout;
while (true) {
    auto rr = fw->try_reserve(opts_.item_size, meta);
    if (rr) { ... commit; return ok; }
    if (rr.status != ringbuffer::reserve_status::full) return map_reserve_status(rr.status);
    if (std::chrono::steady_clock::now() >= deadline) return send_result::timeout;
    std::this_thread::yield();
}
```

After (~10 lines):
```cpp
auto rr = fw->reserve_for(opts_.item_size, timeout, meta);
if (!rr) return map_reserve_status(rr.status);
std::memcpy(rr.payload, data, byte_length);
// zero-padding for fixed...
fw->commit(rr.position);
return send_result::ok;
```

## Files changed

| File | Change |
|------|--------|
| `include/xproc/ringbuffer/fixed_writer.hpp` | Add `reserve_for(size, timeout, meta)` overload, refactor existing `reserve_for` to delegate |
| `include/xproc/ringbuffer/varlen_writer.hpp` | Same |
| `include/xproc/ipc/channel.hpp` | Replace inlined loops in `send_fixed_sized_for` and `send_varlen_for` with `reserve_for` calls |

## What stays unchanged

- `reserve()` blocking method — no change
- `try_reserve()` non-blocking method — no change
- `atomic_backoff` / `atomic_wait` sync primitives — no change
- `commit()` — no change
- Channel public API — no change (same signatures, same behavior)
- All tests and benchmarks — should pass without modification

## Future: atomic_wait_for

A true OS-level timed wait (`FUTEX_WAIT` with timeout, `WaitOnAddress` with timeout,
`os_sync_wait_on_address_with_timeout`) is deferred to a separate change. When added,
`reserve_for` can replace the sleep phase with a single `atomic_wait_for` call,
further improving timeout precision and power efficiency.

## Testing

Existing tests provide coverage:

- `tests/ringbuffer_spsc_test.cpp` — ring buffer correctness
- `tests/producer_backpressure_test.cpp` — backpressure and timeout behavior
- `tests/ipc_integration_test.cpp` — end-to-end IPC
- `tests/message_meta_test.cpp` — metadata roundtrip

No new tests are strictly required since this refactors internals without changing
public API semantics. However, verifying that `send_*_for` with metadata roundtrips
correctly is covered by existing message_meta integration tests.
