# Node Binding C API Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the Node binding (native addon + JS/TS layer) to full parity with the existing C API: message metadata send/peek, observer diagnostics getters, `DiagnosticsTracker`, and the `fixedItemSizeIncompatible` layout-error constant.

**Architecture:** Purely additive changes to the existing N-API addon (`node/src/node_binding.cpp`) plus the JS wrapper (`node/index.js`) and type declarations (`node/index.d.ts`). The binding keeps wrapping only the current `xproc_c.h` surface — no `capi/` changes. Each task is a TDD cycle ending in its own commit.

**Tech Stack:** C++17 / node-addon-api (N-API, `NAPI_DISABLE_CPP_EXCEPTIONS`), plain JavaScript (CommonJS), TypeScript declarations (strict, `noEmit`), `node:test` + `node:assert/strict`, CMake target `xproc_node`.

## Global Constraints

- **No `capi/` changes.** Wrap only functions that already exist in `capi/xproc_c.h`. No consumer-side meta reads. No subprocess.
- **Integer width mapping:** meta input accepts `IntegerLike` (number | bigint); meta output maps u64 fields (`userData`, `timestampNs`) → `bigint`, u32 fields (`schemaId`, `flags`) → `number`.
- **Fixed-channel meta is not transported:** readers observe zero-initialized meta regardless of what was passed to send (matches C semantics).
- **Observer must outlive tracker:** `Observer.close()` throws `TypeError` while any tracker created from it is still open.
- **`LayoutError` union gains `| 11`** (`XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE`).
- **Working tree contains unrelated changes** (rust bindings, subprocess tests, install-fix files). Every commit must `git add` ONLY the task's listed files — never `git add -A` or `git add .`.
- Repo commit style: conventional commits (`feat(node):`, `docs(node):`), each ending with:
  ```
  Co-Authored-By: Claude Code <noreply@anthropic.com>
  ```
- Verification commands (run from repo root `/home/merlot/codes/xproc` unless noted):
  ```bash
  cmake --build build --target xproc_node -j 8
  cd node && node --test --experimental-strip-types test/meta_diagnostics.test.ts   # targeted
  cd node && npm run typecheck
  cd node && npm test                                                              # full suite at task end
  ctest --test-dir build -R xproc_node_smoke
  ```

---

### Task 1: `fixedItemSizeIncompatible` layout-error constant (three layers)

**Files:**
- Modify: `node/src/node_binding.cpp` — `export_constants` (ends ~line 1140)
- Modify: `node/index.js` — `LAYOUT_ERROR` map (lines 36–48)
- Modify: `node/index.d.ts` — `LayoutError` union (line 14), ambient const (~line 385), `LAYOUT_ERROR` type (~line 426)
- Create: `node/test/meta_diagnostics.test.ts`

**Interfaces:**
- Consumes: native export `XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE` from `xproc_c.h` (value 11); existing `xproc.layoutErrorString` (already typed `LayoutError | number`).
- Produces: `xproc.LAYOUT_ERROR.fixedItemSizeIncompatible === 11`; `LayoutError = 0 | 1 | ... | 11`. Tasks 2–4 append tests to the same new test file.

- [ ] **Step 1: Write the failing test**

Create `node/test/meta_diagnostics.test.ts`:

```ts
const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");

const xproc = require("../index.js") as XprocModule;

test("layout error enum includes fixedItemSizeIncompatible", () => {
  assert.equal(xproc.LAYOUT_ERROR.fixedItemSizeIncompatible, 11);
  assert.ok(xproc.layoutErrorString(11).length > 0);
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
```

Expected: FAIL — `AssertionError: Expected values to be strictly equal: undefined !== 11`.

(`npm run typecheck` also fails at this point: property `fixedItemSizeIncompatible` does not exist on `LAYOUT_ERROR`. Both failures are the red signal.)

- [ ] **Step 3: Implement all three layers**

3a. `node/src/node_binding.cpp` — in `export_constants`, after the `XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT` block, add:

```cpp
  exports.Set("XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE));
```

3b. `node/index.js` — in the `LAYOUT_ERROR` map, after the `capacityInsufficient` line, add:

```js
  fixedItemSizeIncompatible: native.XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE,
```

3c. `node/index.d.ts` — three spots:

Line 14, extend the union:

```ts
  type LayoutError = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11;
```

After the `XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT: 10;` ambient const (~line 385), add:

```ts
  const XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE: 11;
```

In the `LAYOUT_ERROR` Readonly type (after `capacityInsufficient`, ~line 426), add:

```ts
    fixedItemSizeIncompatible: typeof XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE;
```

- [ ] **Step 4: Rebuild, verify test and typecheck pass**

```bash
cmake --build /home/merlot/codes/xproc/build --target xproc_node -j 8
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
cd /home/merlot/codes/xproc/node && npm run typecheck
```

Expected: test PASS (1/1), typecheck exits 0 with no output.

- [ ] **Step 5: Commit**

```bash
git add node/src/node_binding.cpp node/index.js node/index.d.ts node/test/meta_diagnostics.test.ts
git commit -m "feat(node): export fixedItemSizeIncompatible layout error constant

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 2: message metadata — send with meta + `peekCopyWithMeta`

**Files:**
- Modify: `node/src/node_binding.cpp` — helpers after `snapshot_to_js` (~line 410); `producer_wrap::send_fixed_sized` / `send_varlen` (~lines 528–578); `observer_wrap` method list (~line 843) + new method after `peek_copy` (~line 910)
- Modify: `node/index.js` — `normalizeMeta` helper after `normalizeOptions` (~line 115); `Producer` wrapper `sendFixedSized`/`sendVarlen` (lines 208–214)
- Modify: `node/index.d.ts` — `MessageMetaInput`/`MessageMeta` after `Snapshot` (~line 257); `Producer` signatures (~lines 283–286); `Observer.peekCopyWithMeta` (~line 330)
- Modify: `node/test/meta_diagnostics.test.ts` — append two tests

**Interfaces:**
- Consumes: `parse_uint64`, `parse_uint32`, `js_bigint_from_u64`, `empty_buffer`, `byte_span_from_js`, `ensure_open`, `throw_type_error`, `throw_xproc_status` (all already exist); C functions `xproc_c_producer_send_fixed_sized_with_meta`, `xproc_c_producer_send_varlen_with_meta`, `xproc_c_observer_peek_copy_with_meta`.
- Produces (relied on by later tasks/tests):
  - native helpers `bool message_meta_from_js(Napi::Env, const Napi::Value&, xproc_c_message_meta*)` and `Napi::Object message_meta_to_js(Napi::Env, const xproc_c_message_meta&)`
  - JS `normalizeMeta(meta)` (internal, not exported)
  - `Producer.sendFixedSized(data: ByteSource, meta?: MessageMetaInput): void`
  - `Producer.sendVarlen(data: ByteSource, meta?: MessageMetaInput): void`
  - `Observer.peekCopyWithMeta(): { payload: Uint8Array; meta: MessageMeta } | null`
  - types `MessageMetaInput`, `MessageMeta`

- [ ] **Step 1: Write the failing tests**

Append to `node/test/meta_diagnostics.test.ts`:

```ts
let shmSequence = 0;

function uniqueShmPath(label: string): string {
  shmSequence += 1;
  return `/xproc_node_meta_${process.pid}_${Date.now()}_${shmSequence}_${label}`;
}

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Ignore cleanup failures when the segment does not exist anymore.
  }
}

test("varlen send with meta roundtrips through peekCopyWithMeta", () => {
  const path = uniqueShmPath("varlen_meta");
  cleanupShm(path);

  const created = xproc.shm.createVarlenChannel({ path, dataCapacity: 32768n });
  const producer = created.openProducer();
  const observer = xproc.shm.attachVarlenChannel({ path }).openObserver();

  try {
    producer.sendVarlen("hello", {
      userData: 0x1122334455667788n,
      timestampNs: 123456789n,
      schemaId: 0xabcd,
      flags: 0x7,
    });

    const peeked = observer.peekCopyWithMeta();
    if (peeked === null) {
      throw new Error("expected peeked message");
    }
    assert.equal(Buffer.from(peeked.payload).toString(), "hello");
    assert.equal(peeked.meta.userData, 0x1122334455667788n);
    assert.equal(peeked.meta.timestampNs, 123456789n);
    assert.equal(peeked.meta.schemaId, 0xabcd);
    assert.equal(peeked.meta.flags, 0x7);
  } finally {
    observer.close();
    producer.close();
    cleanupShm(path);
  }
});

test("fixed-channel send with meta transports zero meta", () => {
  const path = uniqueShmPath("fixed_meta");
  cleanupShm(path);

  const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity: 4096n });
  const producer = created.openProducer();
  const observer = xproc.shm.attachFixedChannel({ path }).openObserver();

  try {
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]), { userData: 99n, flags: 3 });

    const withMeta = observer.peekCopyWithMeta();
    if (withMeta === null) {
      throw new Error("expected peeked message");
    }
    assert.deepEqual([...Buffer.from(withMeta.payload)], [1, 2, 3, 4]);
    assert.equal(withMeta.meta.userData, 0n);
    assert.equal(withMeta.meta.timestampNs, 0n);
    assert.equal(withMeta.meta.schemaId, 0);
    assert.equal(withMeta.meta.flags, 0);

    producer.sendFixedSized(Buffer.from([5, 6, 7, 8]));

    const withoutMeta = observer.peekCopyWithMeta();
    if (withoutMeta === null) {
      throw new Error("expected peeked message");
    }
    assert.deepEqual([...Buffer.from(withoutMeta.payload)], [5, 6, 7, 8]);
    assert.equal(withoutMeta.meta.userData, 0n);
    assert.equal(withoutMeta.meta.flags, 0);
  } finally {
    observer.close();
    producer.close();
    cleanupShm(path);
  }
});
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
```

Expected: enum test PASS; both new tests FAIL (`TypeError: observer.peekCopyWithMeta is not a function`). Typecheck also fails (missing method/type declarations) — that is expected until Step 5.

- [ ] **Step 3: Implement native layer**

3a. In `node/src/node_binding.cpp`, insert after `snapshot_to_js` (the function ending around line 410, before `byte_span_from_js`):

```cpp
bool message_meta_from_js(Napi::Env env, const Napi::Value& value, xproc_c_message_meta* out) {
  if (!value.IsObject() || value.IsArray()) {
    return throw_type_error(env, "meta must be an object");
  }
  const Napi::Object object = value.As<Napi::Object>();
  *out = xproc_c_message_meta{};
  if (object.Has("userData") && !parse_uint64(env, object.Get("userData"), "meta.userData", &out->user_data)) {
    return false;
  }
  if (object.Has("timestampNs") &&
      !parse_uint64(env, object.Get("timestampNs"), "meta.timestampNs", &out->timestamp_ns)) {
    return false;
  }
  if (object.Has("schemaId") && !parse_uint32(env, object.Get("schemaId"), "meta.schemaId", &out->schema_id)) {
    return false;
  }
  if (object.Has("flags") && !parse_uint32(env, object.Get("flags"), "meta.flags", &out->flags)) {
    return false;
  }
  return true;
}

Napi::Object message_meta_to_js(Napi::Env env, const xproc_c_message_meta& meta) {
  Napi::Object object = Napi::Object::New(env);
  object.Set("userData", js_bigint_from_u64(env, meta.user_data));
  object.Set("timestampNs", js_bigint_from_u64(env, meta.timestamp_ns));
  object.Set("schemaId", Napi::Number::New(env, meta.schema_id));
  object.Set("flags", Napi::Number::New(env, meta.flags));
  return object;
}
```

3b. Replace `producer_wrap::send_fixed_sized` body (keep `ensure_open`/arg-count/`byte_span_from_js` preamble; replace only the final C call block):

```cpp
    if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
      xproc_c_message_meta meta{};
      if (!message_meta_from_js(env, info[1], &meta)) {
        return env.Undefined();
      }
      const xproc_c_status status =
          xproc_c_producer_send_fixed_sized_with_meta(handle_, bytes.data, bytes.len, &meta);
      if (status != XPROC_C_STATUS_OK) {
        throw_xproc_status(env, status, "Producer.sendFixedSized");
      }
      return env.Undefined();
    }

    const xproc_c_status status = xproc_c_producer_send_fixed_sized(handle_, bytes.data, bytes.len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Producer.sendFixedSized");
    }
    return env.Undefined();
```

3c. Same replacement for `producer_wrap::send_varlen` final block:

```cpp
    if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
      xproc_c_message_meta meta{};
      if (!message_meta_from_js(env, info[1], &meta)) {
        return env.Undefined();
      }
      const xproc_c_status status = xproc_c_producer_send_varlen_with_meta(handle_, bytes.data, bytes.len, &meta);
      if (status != XPROC_C_STATUS_OK) {
        throw_xproc_status(env, status, "Producer.sendVarlen");
      }
      return env.Undefined();
    }

    const xproc_c_status status = xproc_c_producer_send_varlen(handle_, bytes.data, bytes.len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Producer.sendVarlen");
    }
    return env.Undefined();
```

3d. In `observer_wrap::init`, register the method (after the `peekCopyInto` entry):

```cpp
                           InstanceMethod("peekCopyWithMeta", &observer_wrap::peek_copy_with_meta),
```

3e. Add the method to `observer_wrap` (after `peek_copy_into`), still inside the class:

```cpp
  Napi::Value peek_copy_with_meta(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    xproc_c_message_meta meta{};
    std::uint32_t required_len = 0;
    xproc_c_status status = xproc_c_observer_peek_copy_with_meta(handle_, &meta, nullptr, 0, &required_len);
    if (status == XPROC_C_STATUS_AGAIN) {
      return env.Null();
    }
    if (status == XPROC_C_STATUS_OK) {
      Napi::Object result = Napi::Object::New(env);
      result.Set("payload", empty_buffer(env));
      result.Set("meta", message_meta_to_js(env, meta));
      return result;
    }
    if (status != XPROC_C_STATUS_BUFFER_TOO_SMALL) {
      throw_xproc_status(env, status, "Observer.peekCopyWithMeta");
      return env.Undefined();
    }

    Napi::Buffer<std::uint8_t> buffer = Napi::Buffer<std::uint8_t>::New(env, required_len);
    status = xproc_c_observer_peek_copy_with_meta(handle_, &meta, required_len == 0 ? nullptr : buffer.Data(),
                                                  required_len, &required_len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.peekCopyWithMeta");
      return env.Undefined();
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("payload", buffer);
    result.Set("meta", message_meta_to_js(env, meta));
    return result;
  }
```

- [ ] **Step 4: Implement JS layer**

4a. `node/index.js` — add after the `normalizeOptions` function:

```js
function normalizeMeta(meta) {
  if (meta === undefined) {
    return undefined;
  }
  if (meta == null || typeof meta !== "object" || Array.isArray(meta)) {
    throw new TypeError("meta must be an object");
  }
  const normalized = {};
  for (const key of ["userData", "timestampNs", "schemaId", "flags"]) {
    if (meta[key] !== undefined) {
      normalized[key] = normalizeIntegerLike(meta[key], `meta.${key}`);
    }
  }
  return normalized;
}
```

4b. `node/index.js` — update the `Producer` wrapper methods:

```js
  sendFixedSized(data, meta) {
    return super.sendFixedSized(coerceBuffer(data), normalizeMeta(meta));
  }

  sendVarlen(data, meta) {
    return super.sendVarlen(coerceBuffer(data), normalizeMeta(meta));
  }
```

(`sendFixedBytes` stays one-argument.)

- [ ] **Step 5: Implement type declarations**

5a. `node/index.d.ts` — after the `Snapshot` interface (~line 257):

```ts
  /** Per-message metadata accepted by sends. Missing fields default to 0. */
  interface MessageMetaInput {
    userData?: IntegerLike;
    timestampNs?: IntegerLike;
    schemaId?: IntegerLike;
    flags?: IntegerLike;
  }

  /**
   * Per-message metadata attached to a received payload.
   * u64 fields are bigint; u32 fields are number (`schemaId` is therefore a
   * number here, unlike `Snapshot.schemaId`).
   */
  interface MessageMeta {
    userData: bigint;
    timestampNs: bigint;
    schemaId: number;
    flags: number;
  }
```

5b. `node/index.d.ts` — replace the `Producer` send signatures:

```ts
    /**
     * Sends one fixed-size frame and validates that `data.length === itemSize`.
     * On fixed channels `meta` is accepted for API compatibility but is not
     * transported: readers observe zero-initialized metadata.
     */
    sendFixedSized(data: ByteSource, meta?: MessageMetaInput): void;
    /** Sends one variable-length payload with optional per-message metadata. */
    sendVarlen(data: ByteSource, meta?: MessageMetaInput): void;
```

5c. `node/index.d.ts` — in the `Observer` class (after `peekCopyInto`):

```ts
    /**
     * Peeks the latest visible payload with its per-message metadata, or
     * `null` when the channel is empty.
     */
    peekCopyWithMeta(): { payload: Uint8Array; meta: MessageMeta } | null;
```

- [ ] **Step 6: Rebuild, verify tests and typecheck pass**

```bash
cmake --build /home/merlot/codes/xproc/build --target xproc_node -j 8
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
cd /home/merlot/codes/xproc/node && npm run typecheck
```

Expected: 3/3 tests PASS, typecheck exits 0.

- [ ] **Step 7: Run full suite, then commit**

```bash
cd /home/merlot/codes/xproc/node && npm test
```

Expected: all tests PASS (previous 9 + new 3 = 12).

```bash
git add node/src/node_binding.cpp node/index.js node/index.d.ts node/test/meta_diagnostics.test.ts
git commit -m "feat(node): add message metadata send and peekCopyWithMeta

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 3: observer diagnostics getters

**Files:**
- Modify: `node/src/node_binding.cpp` — `observer_wrap::init` method list; new methods after `peek_copy_with_meta`
- Modify: `node/index.d.ts` — four methods in `Observer`
- Modify: `node/test/meta_diagnostics.test.ts` — append one test

**Interfaces:**
- Consumes: C functions `xproc_c_observer_occupancy_ratio(const xproc_c_observer*, double*)`, `xproc_c_observer_occupancy_bytes` / `xproc_c_observer_available_bytes` / `xproc_c_observer_consumer_lag_bytes` (each `(const xproc_c_observer*, uint64_t*)`); helpers `js_bigint_from_u64`, `ensure_open`, `throw_xproc_status`.
- Produces (used by Task 5 docs/tests):
  - `Observer.occupancyRatio(): number`
  - `Observer.occupancyBytes(): bigint`
  - `Observer.availableBytes(): bigint`
  - `Observer.consumerLagBytes(): bigint`

- [ ] **Step 1: Write the failing test**

Append to `node/test/meta_diagnostics.test.ts`:

```ts
test("observer diagnostics getters report sane values", () => {
  const path = uniqueShmPath("diagnostics");
  cleanupShm(path);

  const dataCapacity = 4096n;
  const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity });
  const producer = created.openProducer();
  const observer = xproc.shm.attachFixedChannel({ path }).openObserver();

  try {
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));

    const ratio = observer.occupancyRatio();
    assert.ok(ratio >= 0 && ratio <= 1);

    const occupied = observer.occupancyBytes();
    assert.equal(typeof occupied, "bigint");
    assert.ok(occupied > 0n);
    assert.ok(occupied <= dataCapacity);

    const available = observer.availableBytes();
    assert.equal(typeof available, "bigint");
    assert.ok(available >= 0n);
    assert.ok(available <= dataCapacity);

    const lag = observer.consumerLagBytes();
    assert.equal(typeof lag, "bigint");
    assert.ok(lag >= 0n);
  } finally {
    observer.close();
    producer.close();
    cleanupShm(path);
  }
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
```

Expected: first 3 tests PASS; new test FAILS with `TypeError: observer.occupancyRatio is not a function`.

- [ ] **Step 3: Implement native methods**

3a. In `observer_wrap::init`, register (after the `peekCopyWithMeta` entry):

```cpp
                           InstanceMethod("occupancyRatio", &observer_wrap::occupancy_ratio),
                           InstanceMethod("occupancyBytes", &observer_wrap::occupancy_bytes),
                           InstanceMethod("availableBytes", &observer_wrap::available_bytes),
                           InstanceMethod("consumerLagBytes", &observer_wrap::consumer_lag_bytes),
```

3b. Add the four methods to `observer_wrap` (after `peek_copy_with_meta`):

```cpp
  Napi::Value occupancy_ratio(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }
    double out = 0.0;
    const xproc_c_status status = xproc_c_observer_occupancy_ratio(handle_, &out);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.occupancyRatio");
      return env.Undefined();
    }
    return Napi::Number::New(env, out);
  }

  Napi::Value occupancy_bytes(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }
    std::uint64_t out = 0;
    const xproc_c_status status = xproc_c_observer_occupancy_bytes(handle_, &out);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.occupancyBytes");
      return env.Undefined();
    }
    return js_bigint_from_u64(env, out);
  }

  Napi::Value available_bytes(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }
    std::uint64_t out = 0;
    const xproc_c_status status = xproc_c_observer_available_bytes(handle_, &out);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.availableBytes");
      return env.Undefined();
    }
    return js_bigint_from_u64(env, out);
  }

  Napi::Value consumer_lag_bytes(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }
    std::uint64_t out = 0;
    const xproc_c_status status = xproc_c_observer_consumer_lag_bytes(handle_, &out);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.consumerLagBytes");
      return env.Undefined();
    }
    return js_bigint_from_u64(env, out);
  }
```

- [ ] **Step 4: Add type declarations**

`node/index.d.ts` — in the `Observer` class, after `peekCopyWithMeta`:

```ts
    /** Returns ring occupancy in [0, 1] as visible to this observer. */
    occupancyRatio(): number;
    /** Returns occupied ring bytes. */
    occupancyBytes(): bigint;
    /** Returns free ring bytes. */
    availableBytes(): bigint;
    /** Returns bytes committed but not yet consumed. */
    consumerLagBytes(): bigint;
```

- [ ] **Step 5: Rebuild, verify test and typecheck pass**

```bash
cmake --build /home/merlot/codes/xproc/build --target xproc_node -j 8
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
cd /home/merlot/codes/xproc/node && npm run typecheck
```

Expected: 4/4 tests PASS, typecheck exits 0.

- [ ] **Step 6: Commit**

```bash
git add node/src/node_binding.cpp node/index.d.ts node/test/meta_diagnostics.test.ts
git commit -m "feat(node): add observer diagnostics getters

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 4: `DiagnosticsTracker` class with observer close guard

**Files:**
- Modify: `node/src/node_binding.cpp` — `observer_wrap` (public accessors, `active_trackers_`, close guard, `create_diagnostics_tracker` declaration); new `diagnostics_tracker_wrap` class after `observer_wrap`; out-of-class definition of `create_diagnostics_tracker`; export in `Init`
- Modify: `node/index.d.ts` — `DiagnosticsTracker` class + `Observer.createDiagnosticsTracker`
- Modify: `node/test/meta_diagnostics.test.ts` — append one test

**Interfaces:**
- Consumes: C functions `xproc_c_diagnostics_tracker_create/update/producer_alive/idle_ms/destroy`; `observer_wrap` internals from Tasks 2–3; `ensure_open`, `throw_type_error`, `throw_xproc_status`, `js_bigint_from_u64`.
- Produces:
  - `Observer.createDiagnosticsTracker(): DiagnosticsTracker`
  - `DiagnosticsTracker` with `update(): void`, `producerAlive(): boolean`, `idleMs(): bigint`, `close(): void` (class exported from the native module; constructor effectively private in typings)
  - `Observer.close()` throws `TypeError` while a tracker is open

- [ ] **Step 1: Write the failing test**

Append to `node/test/meta_diagnostics.test.ts`:

```ts
test("diagnostics tracker detects producer activity and guards observer close", () => {
  const path = uniqueShmPath("tracker");
  cleanupShm(path);

  const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity: 4096n });
  const producer = created.openProducer();
  const observer = xproc.shm.attachFixedChannel({ path }).openObserver();
  let tracker: import("@merlotqi/xproc").DiagnosticsTracker | undefined;

  try {
    tracker = observer.createDiagnosticsTracker();
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));
    tracker.update();

    assert.equal(tracker.producerAlive(), true);
    assert.ok(tracker.idleMs() >= 0n);

    // The observer must outlive the tracker: closing it early is a TypeError.
    assert.throws(
      () => observer.close(),
      (err: unknown) => err instanceof TypeError && String(err).includes("diagnostics tracker"),
    );

    // The observer is still usable while the tracker is open.
    observer.snapshot();

    tracker.close();
    observer.close(); // now succeeds
  } finally {
    tracker?.close(); // idempotent — releases the observer close guard if the body failed early
    observer.close(); // no-op when already closed above
    producer.close();
    cleanupShm(path);
  }
});
```

(`tracker?.close()` is safe to call twice: `reset()` in the native tracker is a no-op once the handle is destroyed. `observer.close()` is likewise idempotent.)

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
```

Expected: first 4 tests PASS; new test FAILS with `TypeError: observer.createDiagnosticsTracker is not a function`.

- [ ] **Step 3: Implement native layer**

3a. In `observer_wrap`, promote/add public accessors (place right after the destructor, before `private:`):

```cpp
 public:
  // Used by diagnostics_tracker_wrap to keep this observer alive and to
  // enforce the C contract that the observer outlives its trackers.
  xproc_c_observer* native_handle() const { return handle_; }
  void retain_tracker() { ++active_trackers_; }
  void release_tracker() {
    if (active_trackers_ > 0u) {
      --active_trackers_;
    }
  }
```

3b. In `observer_wrap`, change `close` to guard (mirrors `Consumer.close` / `active_waits_`):

```cpp
  Napi::Value close(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (active_trackers_ > 0u) {
      throw_type_error(env, "Observer.close cannot be called while a diagnostics tracker is active");
      return env.Undefined();
    }
    reset();
    return env.Undefined();
  }
```

3c. In `observer_wrap`, declare the factory method in the `private:` section (next to the other methods):

```cpp
  Napi::Value create_diagnostics_tracker(const Napi::CallbackInfo& info);
```

Add the member next to `handle_`:

```cpp
  xproc_c_observer* handle_{nullptr};
  std::uint32_t active_trackers_{0};
```

Register in `observer_wrap::init` (after the `consumerLagBytes` entry):

```cpp
                           InstanceMethod("createDiagnosticsTracker", &observer_wrap::create_diagnostics_tracker),
```

3d. After the `observer_wrap` class definition (before `shm_size_for_data_capacity_callback`), add the tracker class:

```cpp
class diagnostics_tracker_wrap final : public Napi::ObjectWrap<diagnostics_tracker_wrap> {
 public:
  static Napi::Function init(Napi::Env env) {
    Napi::Function ctor = DefineClass(env, "DiagnosticsTracker",
                                      {
                                          InstanceMethod("close", &diagnostics_tracker_wrap::close),
                                          InstanceMethod("update", &diagnostics_tracker_wrap::update),
                                          InstanceMethod("producerAlive", &diagnostics_tracker_wrap::producer_alive),
                                          InstanceMethod("idleMs", &diagnostics_tracker_wrap::idle_ms),
                                      });
    constructor = Napi::Persistent(ctor);
    constructor.SuppressDestruct();
    return ctor;
  }

  static Napi::Object NewInstance(Napi::Env env, Napi::Object observer_object) {
    return constructor.New({observer_object});
  }

  explicit diagnostics_tracker_wrap(const Napi::CallbackInfo& info)
      : Napi::ObjectWrap<diagnostics_tracker_wrap>(info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
      throw_type_error(env, "DiagnosticsTracker requires an Observer argument");
      return;
    }
    Napi::Object observer_object = info[0].As<Napi::Object>();
    observer_wrap* observer = Napi::ObjectWrap<observer_wrap>::Unwrap(observer_object);
    if (observer == nullptr || observer->native_handle() == nullptr) {
      throw_type_error(env, "DiagnosticsTracker requires an open Observer");
      return;
    }

    const xproc_c_status status = xproc_c_diagnostics_tracker_create(observer->native_handle(), &handle_);
    if (status != XPROC_C_STATUS_OK) {
      handle_ = nullptr;
      throw_xproc_status(env, status, "DiagnosticsTracker");
      return;
    }
    observer_ = Napi::Persistent(observer_object);
    observer_raw_ = observer;
    observer->retain_tracker();
  }

  ~diagnostics_tracker_wrap() override { reset(); }

 private:
  static Napi::FunctionReference constructor;

  void reset() {
    if (handle_ != nullptr) {
      xproc_c_diagnostics_tracker_destroy(handle_);
      handle_ = nullptr;
      if (observer_raw_ != nullptr) {
        observer_raw_->release_tracker();
        observer_raw_ = nullptr;
      }
      observer_.Reset();
    }
  }

  Napi::Value close(const Napi::CallbackInfo& info) {
    reset();
    return info.Env().Undefined();
  }

  // NOTE: the closed-handle checks below intentionally use throw_type_error
  // (TypeError), not ensure_open (which throws a plain Error). The design spec
  // requires tracker methods called after close() to throw TypeError.
  Napi::Value update(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (handle_ == nullptr) {
      throw_type_error(env, "DiagnosticsTracker is closed");
      return env.Undefined();
    }
    const xproc_c_status status = xproc_c_diagnostics_tracker_update(handle_);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "DiagnosticsTracker.update");
    }
    return env.Undefined();
  }

  Napi::Value producer_alive(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (handle_ == nullptr) {
      throw_type_error(env, "DiagnosticsTracker is closed");
      return env.Undefined();
    }
    bool out = false;
    const xproc_c_status status = xproc_c_diagnostics_tracker_producer_alive(handle_, &out);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "DiagnosticsTracker.producerAlive");
      return env.Undefined();
    }
    return Napi::Boolean::New(env, out);
  }

  Napi::Value idle_ms(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (handle_ == nullptr) {
      throw_type_error(env, "DiagnosticsTracker is closed");
      return env.Undefined();
    }
    std::uint64_t out = 0;
    const xproc_c_status status = xproc_c_diagnostics_tracker_idle_ms(handle_, &out);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "DiagnosticsTracker.idleMs");
      return env.Undefined();
    }
    return js_bigint_from_u64(env, out);
  }

  xproc_c_diagnostics_tracker* handle_{nullptr};
  Napi::ObjectReference observer_;
  observer_wrap* observer_raw_{nullptr};
};

Napi::FunctionReference diagnostics_tracker_wrap::constructor;
```

3e. Define the observer factory out-of-class (immediately after the tracker class):

```cpp
Napi::Value observer_wrap::create_diagnostics_tracker(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!ensure_open(env, handle_)) {
    return env.Undefined();
  }

  Napi::Object instance = diagnostics_tracker_wrap::NewInstance(env, info.This().As<Napi::Object>());
  bool exception_pending = false;
  napi_is_exception_pending(env, &exception_pending);
  if (exception_pending) {
    return env.Undefined();
  }
  return instance;
}
```

3f. In `Init` (after the `Observer` export line ~1162), export the class:

```cpp
  exports.Set("DiagnosticsTracker", diagnostics_tracker_wrap::init(env));
```

- [ ] **Step 4: Add type declarations**

4a. `node/index.d.ts` — in the `Observer` class, after `consumerLagBytes`:

```ts
    /**
     * Creates a diagnostics tracker bound to this observer. The observer must
     * outlive the tracker: `close()` throws while a tracker is active.
     */
    createDiagnosticsTracker(): DiagnosticsTracker;
```

4b. `node/index.d.ts` — declare the class after the `Observer` class:

```ts
  /** Samples observer state to detect producer activity and idle periods. */
  class DiagnosticsTracker {
    private constructor();
    /** Samples the observer's current snapshot into the tracker. */
    update(): void;
    /** Whether the producer committed new messages since the previous update. */
    producerAlive(): boolean;
    /** Milliseconds since the last ring progress (bigint). */
    idleMs(): bigint;
    /** Destroys the native tracker and releases the observer close guard. */
    close(): void;
  }
```

- [ ] **Step 5: Rebuild, verify test and typecheck pass**

```bash
cmake --build /home/merlot/codes/xproc/build --target xproc_node -j 8
cd /home/merlot/codes/xproc/node && node --test --experimental-strip-types test/meta_diagnostics.test.ts
cd /home/merlot/codes/xproc/node && npm run typecheck
```

Expected: 5/5 tests PASS, typecheck exits 0.

- [ ] **Step 6: Run full suite + ctest, then commit**

```bash
cd /home/merlot/codes/xproc/node && npm test
ctest --test-dir /home/merlot/codes/xproc/build -R xproc_node_smoke
```

Expected: all tests PASS; `xproc_node_smoke` Passed.

```bash
git add node/src/node_binding.cpp node/index.d.ts node/test/meta_diagnostics.test.ts
git commit -m "feat(node): add DiagnosticsTracker with observer close guard

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 5: documentation (README + CHANGELOG) and final verification

**Files:**
- Modify: `node/README.md` — insert two sections before `## Development`
- Modify: `CHANGELOG.md` — `Unreleased → Added` entry (file already has an Unreleased section from the install fix; append to `### Added` or create it if absent — current Unreleased has `### Changed` and `### Fixed`, so add a new `### Added` block above `### Changed`)

**Interfaces:**
- Consumes: all APIs from Tasks 1–4 (no new code).
- Produces: documented, released-facing description of the new surface.

- [ ] **Step 1: Update README**

In `node/README.md`, insert immediately before the `## Development` heading:

````markdown
## Message metadata

Variable-length channels transport per-message metadata; fixed channels accept
`meta` for API compatibility but always deliver zeroed metadata to readers.

```ts
const xproc = require("@merlotqi/xproc");

const created = xproc.shm.createVarlenChannel({ path: "/demo-meta", dataCapacity: 32768n });
const producer = created.openProducer();
const observer = xproc.shm.attachVarlenChannel({ path: "/demo-meta" }).openObserver();

producer.sendVarlen("hello", { userData: 42n, flags: 1 });

const peeked = observer.peekCopyWithMeta();
if (peeked !== null) {
  peeked.payload; // Buffer with "hello"
  peeked.meta;    // { userData: 42n, timestampNs: 0n, schemaId: 0, flags: 1 }
}
```

## Diagnostics

```ts
const observer = xproc.shm.attachVarlenChannel({ path: "/demo-meta" }).openObserver();

observer.occupancyRatio(); // number in [0, 1]
observer.occupancyBytes(); // bigint
observer.availableBytes(); // bigint
observer.consumerLagBytes(); // bigint

const tracker = observer.createDiagnosticsTracker();
// ... later, after the producer has been active:
tracker.update();
tracker.producerAlive(); // true when commit_seq advanced since the last update
tracker.idleMs(); // bigint milliseconds without ring progress
tracker.close(); // required before observer.close()
```
````

- [ ] **Step 2: Update CHANGELOG**

In `CHANGELOG.md`, inside `## [Unreleased]`, add a `### Added` section above the existing `### Changed`:

```markdown
### Added

- Node binding parity with the C API: optional per-message metadata on
  `Producer.sendFixedSized`/`sendVarlen`, `Observer.peekCopyWithMeta`,
  observer diagnostics getters (`occupancyRatio`, `occupancyBytes`,
  `availableBytes`, `consumerLagBytes`), `Observer.createDiagnosticsTracker()`
  (with an `Observer.close()` guard while a tracker is open), and the
  `LAYOUT_ERROR.fixedItemSizeIncompatible` constant.
```

- [ ] **Step 3: Final full verification**

```bash
cmake --build /home/merlot/codes/xproc/build --target xproc_node -j 8
cd /home/merlot/codes/xproc/node && npm run typecheck && npm test
ctest --test-dir /home/merlot/codes/xproc/build -R xproc_node_smoke
```

Expected: typecheck clean; `npm test` reports all 14 tests PASS (9 pre-existing + the 5 new tests added by this plan); `xproc_node_smoke` Passed.

- [ ] **Step 4: Commit**

```bash
git add node/README.md CHANGELOG.md
git commit -m "docs(node): document metadata and diagnostics APIs

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```
