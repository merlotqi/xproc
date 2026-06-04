# Message Meta Header Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a mandatory 24-byte `message_meta` header to every ring slot (between ring header and payload), update all send/poll/peek APIs to carry meta, and delete the protocol stubs.

**Architecture:** `message_meta` is a POD struct placed after the ring status/length header and before the payload in every slot. Writers write meta during `reserve()`, readers read meta during `read()`/`peek()`. The poll callback signature changes from `void(void*, uint32_t)` to `void(const message_meta&, void*, uint32_t)` — a breaking change that requires updating all 45+ call sites.

**Tech Stack:** C++20, Google Test

**Branch:** `feat/message-meta` — forked from `main`. All implementation commits land on this branch. Merge to `main` after all tasks pass.

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `include/xproc/ipc/message_meta.hpp` | `message_meta` struct + `message_flags` enum |
| Modify | `include/xproc/ringbuffer/detail/fixed_header.hpp` | Embed meta in fixed slot header |
| Modify | `include/xproc/ringbuffer/detail/varlen_header.hpp` | Embed meta in varlen slot header |
| Modify | `include/xproc/ringbuffer/fixed_writer.hpp` | Write meta during reserve |
| Modify | `include/xproc/ringbuffer/varlen_writer.hpp` | Write meta during reserve |
| Modify | `include/xproc/ringbuffer/fixed_reader.hpp` | Read meta during read/peek |
| Modify | `include/xproc/ringbuffer/varlen_reader.hpp` | Read meta during read/peek |
| Modify | `include/xproc/ipc/channel.hpp` | Add meta send overloads, change poll signature |
| Modify | `include/xproc/ipc/channel_interface.hpp` | Change poll signature |
| Modify | `src/ipc/channel_interface.cpp` | Change poll_impl signature |
| Modify | `include/xproc/ipc/observer.hpp` | Change peek signature |
| Modify | `include/xproc/ipc/runtime.hpp` | Change dispatch callback signature |
| Modify | `include/xproc/ipc/messaging.hpp` | Change poll_decoded, add send_encoded with meta |
| Modify | `include/xproc/ipc/producer.hpp` | Re-export meta send overloads |
| Modify | `include/xproc/ipc/consumer.hpp` | Re-export updated poll |
| Delete | `include/xproc/protocol/json_codec_stub.hpp` | Remove stub |
| Delete | `include/xproc/protocol/protobuf_stub.hpp` | Remove stub |
| Modify | `include/xproc/xproc.hpp` | Remove stub includes, add message_meta include |
| Modify | `capi/xproc_c.h` | Add meta struct, send-with-meta, update poll callback |
| Modify | `capi/xproc_c.cpp` | Implement C API meta functions |
| Modify | 8 test files | Update poll/peek/runtime callback signatures |
| Modify | 12 example files | Update poll/peek/runtime callback signatures |
| Create | `tests/message_meta_test.cpp` | New meta-specific tests |
| Modify | `tests/CMakeLists.txt` | Register new test |

---

## Risks & Mitigations

### R1: Intermediate build breakage between header/writer/reader changes

Changing slot header sizes (Task 2) before updating writers (Task 3) and readers (Task 4) creates a window where `sizeof(header)` changes but read/write offsets are inconsistent. **Mitigation:** Tasks 2, 3, and 4 are merged into a single atomic task (Task 2). All three layers must be committed together so the ring buffer is never in an inconsistent state.

### R2: Memory ordering — meta written before status published

`message_meta` is 24 bytes of non-atomic storage. On weakly-ordered architectures (ARM, POWER), a reader could observe `status == ready` before all 24 meta bytes have landed. **Mitigation:** Writers must issue `std::atomic_thread_fence(std::memory_order_release)` after writing meta and before storing `status` with `memory_order_release`. Readers load `status` with `memory_order_acquire`, which pairs with the writer's release and guarantees meta visibility. This is enforced in the implementation code of Task 2.

```cpp
// Writer (reserve):
h->meta = meta;
std::atomic_thread_fence(std::memory_order_release);
h->status.store(status_ready, std::memory_order_release);

// Reader (read/peek):
if (h->status.load(std::memory_order_acquire) == status_ready) {
  // fence is implicit in the acquire load — meta is now visible
  handler(h->meta, payload_ptr, length);
}
```

### R3: Fixed 24-byte per-slot overhead

For small payloads (e.g. 4-byte integers), meta overhead is 87.5% (28/32 bytes per slot). **Acknowledged.** This is acceptable for the target use case (control-plane IPC). Data-plane channels with tiny payloads can use a dedicated lightweight channel type in the future.

### R4: Mechanical callback migration (45+ sites, error-prone)

Adding `const message_meta&` to every lambda is repetitive and prone to typos. **Mitigation:** A `sed` one-liner is provided in the merged Task 4 to batch-rewrite callback signatures. Manual review follows.

### R5: Protocol stub deletion may break optional_serde_test

`optional_serde_test.cpp` may depend on the stubs being deleted. **Mitigation:** A pre-check step in Task 5 (protocol cleanup) verifies this before deletion.

---

### Task 1: message_meta struct

**Files:**
- Create: `include/xproc/ipc/message_meta.hpp`
- Modify: `include/xproc/xproc.hpp` (add include)

- [ ] **Step 0: Create feature branch from main**

```bash
git -C ${PROJECT_ROOT} checkout main
git -C ${PROJECT_ROOT} checkout -b feat/message-meta
```

- [ ] **Step 1: Create `include/xproc/ipc/message_meta.hpp`**

```cpp
#pragma once

#include <cstdint>

namespace xproc::ipc {

struct message_meta {
  std::uint64_t user_data{0};
  std::uint64_t timestamp_ns{0};
  std::uint32_t schema_id{0};
  std::uint32_t flags{0};
};

enum message_flags : std::uint32_t {
  flag_none          = 0,
  flag_priority_high = 1u << 0,
  flag_compressed    = 1u << 1,
  flag_response      = 1u << 2,
  flag_cancel        = 1u << 3,
};

}  // namespace xproc::ipc
```

- [ ] **Step 2: Add include to `include/xproc/xproc.hpp`**

Add `#include <xproc/ipc/message_meta.hpp>` in the IPC section (after the existing ipc includes).

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build ${PROJECT_ROOT}/build --target xproc 2>&1 | tail -5`
Expected: Build succeeds (header is standalone, no dependencies).

- [ ] **Step 4: Commit**

```bash
git -C ${PROJECT_ROOT} add include/xproc/ipc/message_meta.hpp include/xproc/xproc.hpp
git -C ${PROJECT_ROOT} commit -m "feat: add message_meta struct and message_flags enum"
```

---

### Task 2: Core ring buffer — embed meta, write with ordering, read back

> **CRITICAL:** This task must be committed as a single atomic commit. The header size change, writer update, and reader update are interdependent — splitting them creates a broken intermediate state where `sizeof(header)` has changed but read/write offsets are inconsistent.

**Files:**
- Modify: `include/xproc/ringbuffer/detail/fixed_header.hpp`
- Modify: `include/xproc/ringbuffer/detail/varlen_header.hpp`
- Modify: `include/xproc/ringbuffer/fixed_writer.hpp`
- Modify: `include/xproc/ringbuffer/varlen_writer.hpp`
- Modify: `include/xproc/ringbuffer/fixed_reader.hpp`
- Modify: `include/xproc/ringbuffer/varlen_reader.hpp`

- [ ] **Step 1: Update `fixed_header.hpp`**

Replace contents with:

```cpp
#pragma once

#include <atomic>
#include <xproc/ipc/message_meta.hpp>

namespace xproc::ringbuffer::detail {

struct fixed_message_header {
  std::atomic<uint32_t> status;
  xproc::ipc::message_meta meta;
};

}  // namespace xproc::ringbuffer::detail
```

New size: `4 + 24 = 28` bytes. Payload offset shifts from 4 to 28 bytes after the slot start.

- [ ] **Step 2: Update `varlen_header.hpp`**

Replace contents with:

```cpp
#pragma once

#include <atomic>
#include <xproc/ipc/message_meta.hpp>

namespace xproc::ringbuffer::detail {

struct varlen_message_header {
  std::atomic<uint32_t> status;
  uint32_t length;
  xproc::ipc::message_meta meta;
};

}  // namespace xproc::ringbuffer::detail
```

New size: `4 + 4 + 24 = 32` bytes. Payload offset shifts from 8 to 32 bytes.

- [ ] **Step 3: Update `fixed_writer.hpp`**

Add `#include <chrono>` and `#include <xproc/ipc/message_meta.hpp>` at the top.

Add new `reserve` and `try_reserve` overloads that accept `const message_meta&`:

```cpp
void* reserve(uint32_t item_size, uint64_t& out_pos, const xproc::ipc::message_meta& meta) {
  // ... existing reserve() logic to claim a slot ...
  // Write meta into the slot BEFORE publishing status (see memory ordering below):
  auto* h = reinterpret_cast<detail::fixed_message_header*>(get_ptr(curr_write));
  h->meta = meta;
  if (h->meta.timestamp_ns == 0) {
    h->meta.timestamp_ns = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
  }
  // Memory ordering: fence before status store so reader sees full meta
  std::atomic_thread_fence(std::memory_order_release);
  h->status.store(status_ready, std::memory_order_release);
  return get_ptr(curr_write + sizeof(detail::fixed_message_header));
}
```

The existing `reserve(item_size, out_pos)` delegates to the new overload with `message_meta{}`.

Same pattern for `try_reserve` — new overload with meta parameter.

- [ ] **Step 4: Update `varlen_writer.hpp`**

Same pattern. Add `#include <chrono>` and `#include <xproc/ipc/message_meta.hpp>`.

New `reserve(len, out_pos, meta)` and `try_reserve(len, meta)` overloads that write meta after the varlen header. Apply the same `atomic_thread_fence(release)` before the `status.store(release)`. Existing overloads delegate with `message_meta{}`.

- [ ] **Step 5: Update `fixed_reader.hpp`**

Add `#include <xproc/ipc/message_meta.hpp>`.

Change `read()` handler to load status with acquire semantics and pass meta:

```cpp
auto* h = reinterpret_cast<detail::fixed_message_header*>(get_ptr(curr_read));

// Acquire load pairs with writer's release — guarantees full meta visibility
if (h->status.load(std::memory_order_acquire) == status_ready) {
  handler(h->meta, get_ptr(curr_read + sizeof(detail::fixed_message_header)));
}
```

Change `peek()` handler similarly:

```cpp
// Before: handler(payload_ptr, item_size)
// After:  handler(h->meta, payload_ptr, item_size)
```

- [ ] **Step 6: Update `varlen_reader.hpp`**

Add `#include <xproc/ipc/message_meta.hpp>`.

Change `read()` handler with acquire load:

```cpp
auto* h = reinterpret_cast<detail::varlen_message_header*>(get_ptr(curr_read));
if (h->status.load(std::memory_order_acquire) == status_ready) {
  handler(h->meta, get_ptr(curr_read + sizeof(detail::varlen_message_header)), h->length);
}
```

Change `peek()` handler similarly with acquire semantics.

- [ ] **Step 7: Verify it compiles**

Run: `cmake --build ${PROJECT_ROOT}/build --target xproc 2>&1 | tail -10`
Expected: Build succeeds. Tests will fail due to unchanged callback signatures at higher layers — that's expected and will be fixed in Tasks 3 and 4.

- [ ] **Step 8: Single atomic commit**

```bash
git -C ${PROJECT_ROOT} add \
  include/xproc/ringbuffer/detail/fixed_header.hpp \
  include/xproc/ringbuffer/detail/varlen_header.hpp \
  include/xproc/ringbuffer/fixed_writer.hpp \
  include/xproc/ringbuffer/varlen_writer.hpp \
  include/xproc/ringbuffer/fixed_reader.hpp \
  include/xproc/ringbuffer/varlen_reader.hpp
git -C ${PROJECT_ROOT} commit -m "feat: embed message_meta in ring buffer with release/acquire ordering"
```

---

### Task 3: API layer — channel, observer, runtime, and codec integration

> **RECOMMENDED:** Commit this as a single atomic commit. The channel, observer, runtime, and codec layers all depend on the new ring buffer signatures from Task 2 and form one coherent API surface change.

**Files:**
- Modify: `include/xproc/ipc/channel.hpp`
- Modify: `include/xproc/ipc/channel_interface.hpp`
- Modify: `src/ipc/channel_interface.cpp`
- Modify: `include/xproc/ipc/producer.hpp` (re-export)
- Modify: `include/xproc/ipc/consumer.hpp` (re-export)
- Modify: `include/xproc/ipc/observer.hpp`
- Modify: `include/xproc/ipc/runtime.hpp`
- Modify: `include/xproc/ipc/messaging.hpp`

- [ ] **Step 1: Add meta send overloads to `channel.hpp`**

Add `#include <xproc/ipc/message_meta.hpp>`.

Add overloads for all send methods:

```cpp
void send_fixed_sized(const void* data, std::uint32_t byte_length, const message_meta& meta);
void send_fixed_bytes(const void* data, std::uint32_t payload_len, const message_meta& meta);
template <typename T> void send_fixed(const T& data, const message_meta& meta);
void send_varlen(const void* data, std::uint32_t len, const message_meta& meta);
```

Existing no-meta overloads delegate to meta overloads with `message_meta{}`.

Same for `try_send_*` and `send_*_for` — add meta overloads.

- [ ] **Step 2: Change `poll()` callback signature in `channel.hpp`**

```cpp
// Before: handler(void* payload, uint32_t len)
// After:  handler(const message_meta& meta, void* payload, uint32_t len)
```

For fixed channels, the wrapper lambda changes:
```cpp
// Before: fr->read(item_size, [&](void* p) { invoke(p, item_size); })
// After:  fr->read(item_size, [&](const message_meta& m, void* p) { invoke(m, p, item_size); })
```

For varlen channels, pass through directly since varlen_reader now provides `(meta, ptr, len)`.

- [ ] **Step 3: Update `channel_interface.hpp` and `channel_interface.cpp`**

Change `poll_impl` signature:
```cpp
// Before: virtual bool poll_impl(const std::function<void(void*, std::uint32_t)>& handler) = 0;
// After:  virtual bool poll_impl(const std::function<void(const message_meta&, void*, std::uint32_t)>& handler) = 0;
```

Update `poll()` template to wrap handler with the new signature.

- [ ] **Step 4: Update `producer.hpp` and `consumer.hpp` re-exports**

Re-export the new meta send overloads in `producer.hpp`. Re-export the updated `poll()` signature in `consumer.hpp`. These are thin forwarding headers — add includes and `using` declarations as needed.

- [ ] **Step 5: Update `observer.hpp` peek signature**

```cpp
// Before: handler(const void* payload, uint32_t len)
// After:  handler(const message_meta& meta, const void* payload, uint32_t len)
```

Add `#include <xproc/ipc/message_meta.hpp>`.

- [ ] **Step 6: Update `runtime.hpp` dispatch callbacks**

All dispatch methods (`poll_with_reuse`, `poll_zero_copy`, `poll_with_sbo`) change:

```cpp
// Before: exec([=]() { h(ptr, n); })
// After:  exec([=]() { h(m, ptr, n); })
```

Where `m` is the `message_meta` captured from the poll lambda.

The user-facing handler signature changes:
```cpp
// Before: void(const std::uint8_t* data, std::size_t len)
// After:  void(const message_meta& meta, const std::uint8_t* data, std::size_t len)
```

- [ ] **Step 7: Update `messaging.hpp` — poll_decoded and send_encoded**

Update `poll_decoded` handler signature:
```cpp
// Before: handler(const typename Codec::message_type& msg)
// After:  handler(const message_meta& meta, const typename Codec::message_type& msg)
```

Inside `poll_decoded`, the inner `ch.poll` lambda receives `(const message_meta& m, void* p, uint32_t len)`, decodes, and calls `handler(m, msg)`.

Add `send_encoded` with meta overload:
```cpp
template <typename Codec>
void send_encoded(channel& ch, const typename Codec::message_type& msg, const message_meta& meta);
```

Delegates to the underlying `ch.send_*` with meta.

- [ ] **Step 8: Verify it compiles (tests will fail — expected)**

Run: `cmake --build ${PROJECT_ROOT}/build --target xproc 2>&1 | tail -10`
Expected: Library compiles, but tests and examples fail because poll/peek/runtime callbacks still have the old signature. Fixed in Task 4.

- [ ] **Step 9: Single atomic commit**

```bash
git -C ${PROJECT_ROOT} add \
  include/xproc/ipc/channel.hpp \
  include/xproc/ipc/channel_interface.hpp \
  src/ipc/channel_interface.cpp \
  include/xproc/ipc/producer.hpp \
  include/xproc/ipc/consumer.hpp \
  include/xproc/ipc/observer.hpp \
  include/xproc/ipc/runtime.hpp \
  include/xproc/ipc/messaging.hpp
git -C ${PROJECT_ROOT} commit -m "feat: channel, observer, runtime, and codec all carry message_meta"
```

---

### Task 4: Update all test and example callback signatures (scripted migration)

> **Strategy:** Use `sed` one-liners to batch-rewrite the ~45+ callback sites, then manually review the diff for edge cases (captures, multi-line lambdas, template callbacks). This prevents typos from hand-editing each site.

**Files (tests):**
- Modify: `tests/ipc_observer_attach_test.cpp` (1 poll, 1 peek)
- Modify: `tests/producer_backpressure_test.cpp` (4 poll)
- Modify: `tests/ipc_diagnostics_test.cpp` (1 poll)
- Modify: `tests/socket_transport_test.cpp` (11 poll)
- Modify: `tests/ipc_integration_test.cpp` (4 poll, 1 peek)
- Modify: `tests/api_surface_test.cpp` (3 poll, 1 peek)
- Modify: `tests/win32_wait_shm_test.cpp` (1 poll, 1 peek)
- Modify: `tests/protocol_codec_test.cpp` (1 poll, 3 poll_decoded, 1 send_encoded)
- Modify: `tests/runtime_allocation_test.cpp` (8 runtime::run)
- Modify: `tests/optional_serde_test.cpp` (2 poll_decoded, 2 send_encoded)

**Files (examples):**
- Modify: `examples/socket_varlen_reconnect_demo.cpp` (2 poll)
- Modify: `examples/parent_child_varlen_monitor.cpp` (1 poll)
- Modify: `examples/parent_child_counter_monitor.cpp` (1 poll)
- Modify: `examples/ping_pong.cpp` (1 poll)
- Modify: `examples/mpsc_log_hub_demo.cpp` (1 poll)
- Modify: `examples/fixed_channel_inprocess.cpp` (1 poll)
- Modify: `examples/spmc_fan_out_demo.cpp` (1 poll)
- Modify: `examples/observer_peek_demo.cpp` (1 poll, 1 peek)
- Modify: `examples/mpsc_fan_in_demo.cpp` (1 poll)
- Modify: `examples/handshake_launcher_demo.cpp` (1 poll)
- Modify: `examples/spmc_config_broadcast_demo.cpp` (1 poll)
- Modify: `examples/mpmc_worker_pool_demo.cpp` (1 poll)
- Modify: `examples/parent_child_struct_monitor.cpp` (1 poll)
- Modify: `examples/mpmc_inprocess_bridge_demo.cpp` (1 poll)
- Modify: `examples/varlen_channel_inprocess.cpp` (1 poll)
- Modify: `examples/cpp_python_handshake_progress.cpp` (1 poll)
- Modify: `examples/ipc_taskflow_runtime_demo.cpp` (1 runtime::run)
- Modify: `examples/ipc_taskflow_pipeline_demo.cpp` (1 runtime::run)
- Modify: `examples/runtime_dispatch_demo.cpp` (1 runtime::run)
- Modify: `examples/codec_roundtrip_demo.cpp` (1 poll_decoded, 1 send_encoded)

- [ ] **Step 1: Batch-rewrite poll callbacks with sed**

This handles the common `(void* p, uint32_t len)` and `(void* p, std::uint32_t len)` patterns:

```bash
# Add const xproc::ipc::message_meta& as first parameter to poll-style callbacks
# Pattern: (void* <name>, uint32_t <name>) or (void* <name>, std::uint32_t <name>)
for f in $(grep -rl 'void\*.*uint32_t' tests/ examples/); do
  sed -i -E \
    -e 's/\(void\* ([a-z_][a-z0-9_]*), (std::)?uint32_t ([a-z_][a-z0-9_]*)\)/(const xproc::ipc::message_meta\&, void* \1, \2uint32_t \3)/g' \
    -e 's/\[&\]\(void\* ([a-z_][a-z0-9_]*), (std::)?uint32_t/\[&](const xproc::ipc::message_meta\&, void* \1, \2uint32_t/g' \
    "$f"
done
```

- [ ] **Step 2: Batch-rewrite peek callbacks with sed**

```bash
# Pattern: (const void* <name>, uint32_t <name>)
for f in $(grep -rl 'const void\*.*uint32_t' tests/ examples/); do
  sed -i -E \
    -e 's/\(const void\* ([a-z_][a-z0-9_]*), (std::)?uint32_t ([a-z_][a-z0-9_]*)\)/(const xproc::ipc::message_meta\&, const void* \1, \2uint32_t \3)/g' \
    "$f"
done
```

- [ ] **Step 3: Batch-rewrite runtime::run callbacks with sed**

```bash
# Pattern: (const std::uint8_t* <name>, std::size_t <name>)
for f in $(grep -rl 'const std::uint8_t\*.*std::size_t' tests/ examples/); do
  sed -i -E \
    -e 's/\(const std::uint8_t\* ([a-z_][a-z0-9_]*), std::size_t ([a-z_][a-z0-9_]*)\)/(const xproc::ipc::message_meta\&, const std::uint8_t* \1, std::size_t \2)/g' \
    "$f"
done
```

- [ ] **Step 4: Batch-rewrite poll_decoded callbacks with sed**

```bash
# Pattern: (const Codec::message_type& <name>) — captures single template arg
for f in $(grep -rl 'const.*::message_type&' tests/ examples/); do
  sed -i -E \
    -e 's/\(const ([a-zA-Z_:]+)\& ([a-z_][a-z0-9_]*)\)/(const xproc::ipc::message_meta\&, const \1\& \2)/g' \
    "$f"
done
```

- [ ] **Step 5: Manual review of the diff**

```bash
git -C ${PROJECT_ROOT} diff tests/ examples/
```

Look for:
- Multi-line lambdas that sed couldn't match (the lambda body spans lines after the capture)
- `send_encoded` call sites (added `message_meta` parameter to the call, not the callback)
- Template-deduced callbacks where the type isn't spelled out explicitly
- Discard patterns: replace `(void*, std::uint32_t)` with `(const xproc::ipc::message_meta&, void*, std::uint32_t)` if sed missed them

Fix any mismatches manually.

- [ ] **Step 6: Build, run tests, and build examples**

```bash
cmake --build ${PROJECT_ROOT}/build --target xproc_run_tests 2>&1 | tail -10
cmake --build ${PROJECT_ROOT}/build 2>&1 | tail -5
```
Expected: All tests PASS. All examples compile.

- [ ] **Step 7: Commit**

```bash
git -C ${PROJECT_ROOT} add tests/ examples/
git -C ${PROJECT_ROOT} commit -m "fix: update all test and example callbacks for message_meta signature change"
```

---

### Task 5: Protocol cleanup — delete stubs

**Files:**
- Delete: `include/xproc/protocol/json_codec_stub.hpp`
- Delete: `include/xproc/protocol/protobuf_stub.hpp`
- Modify: `include/xproc/xproc.hpp` (remove stub includes)

- [ ] **Step 0: Pre-check — verify `optional_serde_test` does not depend on stubs**

```bash
# Check if the test includes either stub
grep -rE 'json_codec_stub|protobuf_stub' ${PROJECT_ROOT}/tests/optional_serde_test.cpp
```

If matches are found, the test must be refactored before proceeding. Options:
- Replace stub types with real codec types from `codecs.hpp`
- Gate the affected test cases behind `#ifdef XPROC_WITH_NLOHMANN_JSON`
- Inline the stub's thin wrapping logic directly in the test

Do NOT proceed past this step until `optional_serde_test.cpp` compiles without the stubs.

- [ ] **Step 1: Remove stub includes from `xproc.hpp`**

Remove these two lines:
```cpp
#include <xproc/protocol/json_codec_stub.hpp>
#include <xproc/protocol/protobuf_stub.hpp>
```

- [ ] **Step 2: Delete the stub files**

```bash
git -C ${PROJECT_ROOT} rm include/xproc/protocol/json_codec_stub.hpp include/xproc/protocol/protobuf_stub.hpp
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build ${PROJECT_ROOT}/build --target xproc_run_tests 2>&1 | tail -5`
Expected: Build succeeds. If `optional_serde_test.cpp` uses `nlohmann_json_codec`, check whether it's gated behind `XPROC_WITH_NLOHMANN_JSON`. If so, it may need the stub — verify before deleting. (The stub is a thin wrapper; the test may need to inline the codec or use `codecs.hpp` types instead.)

- [ ] **Step 4: Commit**

```bash
git -C ${PROJECT_ROOT} add include/xproc/protocol/ include/xproc/xproc.hpp
git -C ${PROJECT_ROOT} commit -m "chore: remove json_codec_stub and protobuf_stub from protocol module"
```

---

### Task 6: C API — add meta struct, send-with-meta, update poll callback

**Files:**
- Modify: `capi/xproc_c.h`
- Modify: `capi/xproc_c.cpp`

- [ ] **Step 1: Add `xproc_c_message_meta` struct to `xproc_c.h`**

```c
typedef struct xproc_c_message_meta {
  uint64_t user_data;
  uint64_t timestamp_ns;
  uint32_t schema_id;
  uint32_t flags;
} xproc_c_message_meta;
```

- [ ] **Step 2: Update `xproc_c_poll_handler` typedef**

```c
// Before: typedef void (*xproc_c_poll_handler)(void* payload, uint32_t len, void* user_data);
// After:  typedef void (*xproc_c_poll_handler)(const xproc_c_message_meta* meta, void* payload, uint32_t len, void* user_data);
```

- [ ] **Step 3: Add send-with-meta function declaration**

```c
XPROC_C_API xproc_c_status xproc_c_producer_send_fixed_sized_with_meta(
    xproc_c_producer* producer, const void* data, uint32_t byte_length,
    const xproc_c_message_meta* meta);
```

- [ ] **Step 4: Add peek-with-meta function declaration**

```c
XPROC_C_API xproc_c_status xproc_c_observer_peek_copy_with_meta(
    xproc_c_observer* observer, xproc_c_message_meta* out_meta,
    void* buffer, uint32_t buffer_capacity, uint32_t* out_len);
```

- [ ] **Step 5: Implement C API functions in `xproc_c.cpp`**

Add conversion helper:
```cpp
static xproc::ipc::message_meta to_cpp_meta(const xproc_c_message_meta* m) {
  if (!m) return {};
  return {m->user_data, m->timestamp_ns, m->schema_id, m->flags};
}
static void to_c_meta(const xproc::ipc::message_meta& m, xproc_c_message_meta* out) {
  out->user_data = m.user_data;
  out->timestamp_ns = m.timestamp_ns;
  out->schema_id = m.schema_id;
  out->flags = m.flags;
}
```

Implement `xproc_c_producer_send_fixed_sized_with_meta` — convert C meta to C++ meta, call `producer->impl->send_fixed_sized(data, byte_length, cpp_meta)`.

Implement `xproc_c_observer_peek_copy_with_meta` — call `observer->impl->peek` with lambda that captures meta and copies payload.

Update existing `xproc_c_consumer_poll_copy` to pass meta to the C callback.

- [ ] **Step 6: Build and run C API tests**

Run: `cmake --build ${PROJECT_ROOT}/build --target xproc_capi_smoke_tests && ${PROJECT_ROOT}/build/tests/xproc_capi_smoke_tests`
Expected: All tests PASS.

- [ ] **Step 7: Commit**

```bash
git -C ${PROJECT_ROOT} add capi/xproc_c.h capi/xproc_c.cpp
git -C ${PROJECT_ROOT} commit -m "feat: C API gains message_meta struct, send-with-meta, updated poll callback"
```

---

### Task 7: New meta-specific tests

**Files:**
- Create: `tests/message_meta_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add `message_meta_test.cpp` to `tests/CMakeLists.txt`**

Add to `XPROC_GTEST_TEST_FILES_COMMON` list.

- [ ] **Step 2: Create `tests/message_meta_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <xproc/xproc.hpp>

static xproc::ipc::transport_options make_opts(const std::string& path, std::size_t cap,
                                                xproc::ipc::channel_type type) {
  xproc::core::shm::unlink(path);
  xproc::ipc::transport_options o;
  o.path = path;
  o.shm_size = sizeof(xproc::core::control_block) + cap;
  o.type = type;
  o.item_size = sizeof(std::uint32_t);
  return o;
}

TEST(MessageMeta, MetaDefaultValues) {
  xproc::ipc::message_meta m{};
  EXPECT_EQ(m.user_data, 0u);
  EXPECT_EQ(m.timestamp_ns, 0u);
  EXPECT_EQ(m.schema_id, 0u);
  EXPECT_EQ(m.flags, 0u);
}

TEST(MessageMeta, FixedSlotContainsMeta) {
  const std::string path = "/xproc_meta_fixed";
  auto opts = make_opts(path, 4096, xproc::ipc::channel_type::fixed);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::consumer cons(opts);
    prod.send_fixed<std::uint32_t>(42u);
    bool got = false;
    cons.poll([&](const xproc::ipc::message_meta& m, void*, std::uint32_t) {
      EXPECT_NE(m.timestamp_ns, 0u);
      got = true;
    });
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

TEST(MessageMeta, VarlenSlotContainsMeta) {
  const std::string path = "/xproc_meta_varlen";
  auto opts = make_opts(path, 4096, xproc::ipc::channel_type::varlen);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::consumer cons(opts);
    prod.send_varlen("hello", 5);
    bool got = false;
    cons.poll([&](const xproc::ipc::message_meta& m, void*, std::uint32_t) {
      EXPECT_NE(m.timestamp_ns, 0u);
      got = true;
    });
    EXPECT_TRUE(got);
  }
  xproc::core::shm::unlink(path);
}

TEST(MessageMeta, MetaUserDataRoundtrip) {
  const std::string path = "/xproc_meta_userdata";
  auto opts = make_opts(path, 4096, xproc::ipc::channel_type::fixed);
  {
    xproc::ipc::message_meta sent{};
    sent.user_data = 42;
    xproc::ipc::producer prod(opts);
    xproc::ipc::consumer cons(opts);
    prod.send_fixed<std::uint32_t>(1u, sent);
    cons.poll([&](const xproc::ipc::message_meta& m, void*, std::uint32_t) {
      EXPECT_EQ(m.user_data, 42u);
    });
  }
  xproc::core::shm::unlink(path);
}

TEST(MessageMeta, MetaFlagsRoundtrip) {
  const std::string path = "/xproc_meta_flags";
  auto opts = make_opts(path, 4096, xproc::ipc::channel_type::varlen);
  {
    xproc::ipc::message_meta sent{};
    sent.flags = xproc::ipc::flag_priority_high | xproc::ipc::flag_compressed;
    xproc::ipc::producer prod(opts);
    xproc::ipc::consumer cons(opts);
    prod.send_varlen("x", 1, sent);
    cons.poll([&](const xproc::ipc::message_meta& m, void*, std::uint32_t) {
      EXPECT_EQ(m.flags, sent.flags);
    });
  }
  xproc::core::shm::unlink(path);
}

TEST(MessageMeta, MetaSchemaIdRoundtrip) {
  const std::string path = "/xproc_meta_schema";
  auto opts = make_opts(path, 4096, xproc::ipc::channel_type::varlen);
  {
    xproc::ipc::message_meta sent{};
    sent.schema_id = 99;
    xproc::ipc::producer prod(opts);
    xproc::ipc::consumer cons(opts);
    prod.send_varlen("x", 1, sent);
    cons.poll([&](const xproc::ipc::message_meta& m, void*, std::uint32_t) {
      EXPECT_EQ(m.schema_id, 99u);
    });
  }
  xproc::core::shm::unlink(path);
}

TEST(MessageMeta, DefaultMetaHasTimestamp) {
  const std::string path = "/xproc_meta_default_ts";
  auto opts = make_opts(path, 4096, xproc::ipc::channel_type::fixed);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::consumer cons(opts);
    prod.send_fixed<std::uint32_t>(1u);  // no meta — auto-fill
    cons.poll([&](const xproc::ipc::message_meta& m, void*, std::uint32_t) {
      EXPECT_NE(m.timestamp_ns, 0u);
    });
  }
  xproc::core::shm::unlink(path);
}

TEST(MessageMeta, DefaultMetaHasZeroUserData) {
  const std::string path = "/xproc_meta_default_ud";
  auto opts = make_opts(path, 4096, xproc::ipc::channel_type::fixed);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::consumer cons(opts);
    prod.send_fixed<std::uint32_t>(1u);
    cons.poll([&](const xproc::ipc::message_meta& m, void*, std::uint32_t) {
      EXPECT_EQ(m.user_data, 0u);
      EXPECT_EQ(m.flags, 0u);
      EXPECT_EQ(m.schema_id, 0u);
    });
  }
  xproc::core::shm::unlink(path);
}

// --- Concurrent stress tests ---

TEST(MessageMeta, ConcurrentSingleWriterMultipleReaders) {
  const std::string path = "/xproc_meta_conc_swmr";
  auto opts = make_opts(path, 65536, xproc::ipc::channel_type::fixed);
  xproc::core::shm::unlink(path);

  constexpr int kMessages = 1000;
  std::atomic<int> received{0};
  std::atomic<int> meta_checks_passed{0};

  auto writer = std::thread([&] {
    xproc::ipc::producer prod(opts);
    for (int i = 0; i < kMessages; ++i) {
      xproc::ipc::message_meta m{};
      m.user_data = static_cast<uint64_t>(i);
      m.flags = xproc::ipc::flag_priority_high;
      while (!prod.try_send_fixed(static_cast<uint32_t>(i), m)) {
        std::this_thread::yield();
      }
    }
  });

  auto reader = std::thread([&] {
    xproc::ipc::consumer cons(opts);
    while (received.load(std::memory_order_acquire) < kMessages) {
      cons.poll([&](const xproc::ipc::message_meta& m, void* p, std::uint32_t) {
        auto val = *static_cast<uint32_t*>(p);
        if (m.user_data == val && (m.flags & xproc::ipc::flag_priority_high)) {
          meta_checks_passed.fetch_add(1, std::memory_order_relaxed);
        }
        received.fetch_add(1, std::memory_order_release);
      });
    }
  });

  writer.join();
  reader.join();

  EXPECT_EQ(received.load(), kMessages);
  EXPECT_EQ(meta_checks_passed.load(), kMessages);
  xproc::core::shm::unlink(path);
}

TEST(MessageMeta, ConcurrentMultiWriterMultiReader) {
  const std::string path = "/xproc_meta_conc_mwmr";
  auto opts = make_opts(path, 65536, xproc::ipc::channel_type::varlen);
  xproc::core::shm::unlink(path);

  constexpr int kWriters = 3;
  constexpr int kReaders = 2;
  constexpr int kMsgsPerWriter = 200;
  std::atomic<int> total_sent{0};
  std::atomic<int> total_received{0};
  std::atomic<int> meta_mismatches{0};

  std::vector<std::thread> writers;
  for (int w = 0; w < kWriters; ++w) {
    writers.emplace_back([&, w] {
      xproc::ipc::producer prod(opts);
      for (int i = 0; i < kMsgsPerWriter; ++i) {
        xproc::ipc::message_meta m{};
        m.user_data = static_cast<uint64_t>(w * 10000 + i);
        m.schema_id = static_cast<uint32_t>(w);
        std::string payload = "writer_" + std::to_string(w) + "_msg_" + std::to_string(i);
        while (!prod.try_send_varlen(payload.data(), static_cast<uint32_t>(payload.size()), m)) {
          std::this_thread::yield();
        }
        total_sent.fetch_add(1, std::memory_order_release);
      }
    });
  }

  std::vector<std::thread> readers;
  for (int r = 0; r < kReaders; ++r) {
    readers.emplace_back([&] {
      xproc::ipc::consumer cons(opts);
      while (total_received.load(std::memory_order_acquire) < kWriters * kMsgsPerWriter) {
        cons.poll([&](const xproc::ipc::message_meta& m, void* p, std::uint32_t len) {
          // Verify meta integrity: schema_id should be < kWriters
          if (m.schema_id >= static_cast<uint32_t>(kWriters)) {
            meta_mismatches.fetch_add(1, std::memory_order_relaxed);
          }
          // Verify timestamp was filled
          if (m.timestamp_ns == 0) {
            meta_mismatches.fetch_add(1, std::memory_order_relaxed);
          }
          total_received.fetch_add(1, std::memory_order_release);
          (void)p;
          (void)len;
        });
      }
    });
  }

  for (auto& t : writers) t.join();
  for (auto& t : readers) t.join();

  EXPECT_EQ(total_received.load(), kWriters * kMsgsPerWriter);
  EXPECT_EQ(meta_mismatches.load(), 0);
  xproc::core::shm::unlink(path);
}
```

- [ ] **Step 3: Build and run**

Run: `cmake --build ${PROJECT_ROOT}/build --target xproc_message_meta_test && ${PROJECT_ROOT}/build/tests/xproc_message_meta_test`
Expected: All 10 tests PASS (8 unit + 2 concurrent stress).

- [ ] **Step 4: Commit**

```bash
git -C ${PROJECT_ROOT} add tests/message_meta_test.cpp tests/CMakeLists.txt
git -C ${PROJECT_ROOT} commit -m "test: add message_meta roundtrip, default-value, and concurrent stress tests"
```

---

### Task 8: C API meta tests

**Files:**
- Modify: `tests/capi_smoke_test.cpp`

- [ ] **Step 1: Add C API meta smoke tests**

Append to `tests/capi_smoke_test.cpp`:

```cpp
TEST(CApiSmoke, CSendWithMetaSmoke) {
  const char* path = "/xproc_capi_meta_send";
  xproc_c_shm_unlink(path);
  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path;
  opts.shm_size = xproc_c_shm_size_for_data_capacity(4096);
  opts.type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(uint32_t);

  xproc_c_producer* prod = nullptr;
  xproc_c_consumer* cons = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&opts, &prod), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_consumer_open(&opts, &cons), XPROC_C_STATUS_OK);

  xproc_c_message_meta meta{};
  meta.user_data = 123;
  meta.flags = 0x5;
  uint32_t val = 42;
  ASSERT_EQ(xproc_c_producer_send_fixed_sized_with_meta(prod, &val, sizeof(val), &meta), XPROC_C_STATUS_OK);

  // poll_copy callback receives meta
  xproc_c_message_meta received_meta{};
  bool got = false;
  // Use the updated xproc_c_consumer_poll_copy with meta-aware callback
  // (exact API depends on how poll_copy is updated)

  xproc_c_consumer_close(cons);
  xproc_c_producer_close(prod);
  xproc_c_shm_unlink(path);
}
```

(Final test code depends on the exact C API poll_copy signature update in Task 6.)

- [ ] **Step 2: Build and run**

Run: `cmake --build ${PROJECT_ROOT}/build --target xproc_capi_smoke_tests && ${PROJECT_ROOT}/build/tests/xproc_capi_smoke_tests`

- [ ] **Step 3: Commit**

```bash
git -C ${PROJECT_ROOT} add tests/capi_smoke_test.cpp
git -C ${PROJECT_ROOT} commit -m "test: add C API message_meta smoke tests"
```
