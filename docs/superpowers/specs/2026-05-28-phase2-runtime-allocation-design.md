# Phase 2 P0: Runtime Allocation Improvement Design

Date: 2026-05-28
Branch target: `fix/binding-error`
Phase 2 reference: [2026-05-28-phase2-reference-design.md](2026-05-28-phase2-reference-design.md)
Implementation target: `feat/runtime-allocation`

## Objective

Eliminate the per-message heap allocation in `ipc::runtime::run()` and provide configurable copy policies so callers can tune the dispatch path for their workload.

## Problem Statement

### Current behavior

`ipc::runtime::run()` ([include/xproc/ipc/runtime.hpp](include/xproc/ipc/runtime.hpp)) copies every polled message into a fresh `std::vector<uint8_t>` before submitting it to the executor:

```cpp
// runtime.hpp:44-45 (shm_ path) and :48-49 (iface_ path) — identical pattern
std::vector<std::uint8_t> copy_data(static_cast<std::uint8_t*>(ptr),
                                    static_cast<std::uint8_t*>(ptr) + len);
pool_executor([data = std::move(copy_data), h]() mutable {
    h(data.data(), data.size());
});
```

This is simple and safe -- ownership transfers cleanly into the async worker. But for sustained high-throughput workloads (e.g., millions of small messages per second), the per-message allocation dominates CPU profiles.

### Why this is P0

Every integration that uses `ipc::runtime` hits this path. There is no workaround short of bypassing `runtime` entirely and writing a custom poll loop. Fixing this unlocks the common high-throughput case for all users.

## Design Space

Three copy-policy options cover the relevant usage patterns:

| Policy | Allocation | Lifetime | Use case |
|--------|-----------|----------|----------|
| **Eager copy with buffer reuse** | Zero (amortized) | Until handler returns | Default. Reuses a growable internal buffer. |
| **Zero-copy view** | Zero | Until next poll / handler return | Caller can process data synchronously or copy to its own storage. |
| **Small-buffer optimization (SBO)** | Zero for messages ≤ N bytes | Until handler returns | Stack-allocated for tiny messages, heap fallback for large ones. |

Batching and backpressure are orthogonal layers that compose with any copy policy.

## Deliverables

### 1. Buffer reuse (default policy)

Replace the per-message `std::vector<uint8_t>` with a reusable internal buffer:

- The `runtime` owns a single `std::vector<uint8_t>` that grows to the maximum message size seen so far.
- Each poll copies message bytes into that buffer (resizing only when a larger message arrives).
- The executor lambda receives a `std::pair<uint8_t*, uint32_t>` or a `span` -- callers that need ownership beyond the handler return must copy explicitly.
- This is a behavior change: currently the executor receives an owning vector that outlives the handler. Under buffer reuse, the pointer is valid only for the duration of the handler call.

**Migration note:** The existing API signature `h(data.data(), data.size())` with `void(const void*, uint32_t)` handler contract does not change. Only the lifetime guarantee changes -- which was already the case for the zero-copy `poll` handler on `channel` itself.

### 2. Copy-policy hooks

Add a `copy_policy` enum and a policy-aware `run()` overload:

```cpp
enum class copy_policy {
    reuse_buffer,   // Default. Reuse internal buffer. Handler must copy if it needs ownership.
    zero_copy,      // Pass ring-buffer pointer directly. Valid only until next poll.
    sbo             // Stack-allocate up to N bytes, heap-allocate beyond that.
};

template <typename Executor, typename Handler>
void run(Executor&& pool_executor, Handler&& handler, copy_policy policy = copy_policy::reuse_buffer);
```

- `reuse_buffer`: Copies into internal buffer, submits pointer+len. Pointer invalid after handler returns.
- `zero_copy`: Submits `ptr` and `len` directly from the ring buffer. Valid only during the executor lambda invocation. Fastest path, caller must copy if async.
- `sbo`: Template parameter `N` for the small-buffer threshold. Messages ≤ N bytes are stack-copied into the lambda capture. Larger messages fall back to heap.

### 3. Batching mode

Add optional batching to amortize executor submission cost:

```cpp
template <typename Executor, typename Handler>
void run_batched(Executor&& pool_executor, Handler&& handler,
                 std::size_t max_batch_size = 1,
                 copy_policy policy = copy_policy::reuse_buffer);
```

- Polls up to `max_batch_size` messages in one pass.
- Submits a single executor task with all batched messages.
- The handler is invoked once per message within the batch.
- Batching trades latency for throughput -- useful when executor submission is expensive (e.g., cross-thread queue).

### 4. Backpressure signaling

Add an optional backpressure callback:

```cpp
template <typename Executor, typename Handler, typename Backpressure>
void run(Executor&& pool_executor, Handler&& handler,
         Backpressure&& on_backpressure,
         copy_policy policy = copy_policy::reuse_buffer);
```

- `on_backpressure(size_t queued_count)` is called when messages are accumulating faster than the executor drains them.
- The callback can throttle the producer (e.g., via a semaphore or sleep).
- Default is a no-op (current behavior).

## Non-Goals

- Changing the `channel::poll()` or `consumer_channel_interface::poll()` signatures
- Adding thread-safety to `runtime` (it remains single-threaded by contract)
- Supporting multiple executors or handler dispatch strategies beyond the current `pool_executor` model
- Modifying `wait()` or `stop()` semantics -- those are addressed by the socket resilience P1 tier

## API Compatibility

| API surface | Change |
|-------------|--------|
| `runtime(channel&)` | No change |
| `runtime(consumer&)` | No change |
| `runtime(consumer_channel_interface&)` | No change |
| `run(Executor&&, Handler&&)` | **Breaking:** pointer lifetime changes from owning-vector to handler-duration. Callers that stored the vector beyond handler return must now copy explicitly. |
| `stop()` | No change |

The existing two-argument `run()` overload changes the handler contract from "receives owning data" to "receives borrowed data." This is the same contract that `channel::poll()` itself uses, so callers that already copy inside the handler see no change. Callers that moved the vector into long-lived storage will need to copy explicitly.

## Success Criteria

- [ ] Buffer reuse eliminates per-message heap allocation in the default policy (verified via benchmark)
- [ ] Zero-copy policy passes ring-buffer pointers directly with no intermediate copy
- [ ] SBO policy uses stack allocation for messages ≤ N bytes, heap for larger ones
- [ ] Batching mode submits one executor task for up to `max_batch_size` messages
- [ ] Backpressure callback fires when pending work exceeds threshold
- [ ] Existing `runtime` tests pass with the new default policy
- [ ] Benchmark shows ≥2x throughput improvement for typical workloads under buffer reuse

## Evidence Sources

- `include/xproc/ipc/runtime.hpp` -- current implementation
- `include/xproc/ipc/channel.hpp` -- `poll()` handler contract (zero-copy, handler-duration lifetime)
- `include/xproc/ipc/channel_interface.hpp` -- polymorphic consumer poll contract
- `tests/api_surface_test.cpp` -- existing runtime test coverage
- `benchmarks/ipc_benchmark.cpp` -- existing IPC benchmarks

## Transition Rule

After this spec is reviewed and approved, the next step is to write the implementation plan using the `writing-plans` skill, targeting a new `feat/runtime-allocation` branch from `main`.
