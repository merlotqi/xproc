# Node API and @merlot/xproc Publishing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship ergonomic Node shared-memory and socket APIs plus a scoped `@merlot/xproc` npm package that installs and loads directly from bundled prebuilt binaries on the approved mainstream platforms.

**Architecture:** Add one small C API helper for manifest-backed shared-memory option discovery, then layer new `xproc.shm` and `xproc.socket` helpers on top of the existing raw Node constructors. Split native loading into a package-first prebuild path using `node-gyp-build` with a repository-build fallback, and add packaging/CI workflows that verify the actual packed tarball in a clean install before publish.

**Tech Stack:** Node.js CommonJS, N-API / node-addon-api, xproc C API, CMake/CTest, GitHub Actions, npm trusted publishing (OIDC), TypeScript declaration files.

## Execution Status

- Completed on 2026-04-26: Task 1 through Task 4, including scoped package wiring for `@merlot/xproc`, staged prebuild loading, and packed-tarball install verification.
- Example migration is being split into multiple implementation commits:
  - SHM example migration: `01daa0e`
  - Socket loopback examples: `0bb6df6`
- Remaining implementation work in this plan: rewrite `node/README.md`, update CI coverage for packaged Node artifacts, and add the publish workflow.

---

## File Structure

- Create: `node/lib/load-native.js`
- Create: `node/LICENSE`
- Create: `node/scripts/stage-prebuild.js`
- Create: `node/scripts/assert-packlist.js`
- Create: `node/scripts/verify-packed-install.js`
- Create: `node/test/high_level_shm.test.ts`
- Create: `node/test/high_level_socket.test.ts`
- Create: `node/examples/socket_fixed_loopback.ts`
- Create: `node/examples/socket_varlen_loopback.ts`
- Create: `.github/workflows/node-package-publish.yml`
- Modify: `capi/xproc_c.h`
- Modify: `capi/xproc_c.cpp`
- Modify: `tests/capi_smoke_test.cpp`
- Modify: `node/src/node_binding.cpp`
- Modify: `node/index.js`
- Modify: `node/index.d.ts`
- Modify: `node/package.json`
- Modify: `node/package-lock.json`
- Modify: `node/.gitignore`
- Modify: `node/README.md`
- Modify: `node/examples/fixed_channel_inprocess.ts`
- Modify: `node/examples/varlen_channel_inprocess.ts`
- Modify: `node/examples/observer_peek_demo.ts`
- Modify: `.github/workflows/ci.yml`

## Scope Notes

- The publish target is `@merlot/xproc`, not unscoped `xproc`, because the unscoped package name is already taken on npm.
- The supported binary matrix in this plan is:
  - `linux-x64` glibc
  - `linux-arm64` glibc
  - `darwin-x64`
  - `darwin-arm64`
  - `win32-x64`
- musl / Alpine is intentionally out of scope.
- The raw `Producer` / `Consumer` / `Observer` API remains supported in this plan.
- The new high-level SHM attach APIs require lower-level manifest discovery support, so this plan adds one small C API helper rather than forcing callers to repeat `itemSize`.

### Task 1: Add a C API Helper for Reading Existing SHM Options

**Files:**
- Modify: `capi/xproc_c.h`
- Modify: `capi/xproc_c.cpp`
- Modify: `tests/capi_smoke_test.cpp`
- Reference: `include/xproc/ipc/shm_builders.hpp`

- [ ] **Step 1: Write the failing C API smoke test**

Add a new test case to `tests/capi_smoke_test.cpp` that creates a fixed SHM segment, calls a new `xproc_c_shm_read_existing_options(...)` helper, and asserts that the inferred type, item size, schema id, creator metadata, and `create_if_missing=false` are returned.

```cpp
TEST(CApiSmoke, ReadExistingShmOptionsInfersManifestBackedFields) {
  const std::string path = "/xproc_capi_read_existing_options";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options creator{};
  xproc_c_options_init(&creator);
  creator.path = path.c_str();
  creator.shm_size = xproc_c_shm_size_for_data_capacity(8192);
  creator.channel_type = XPROC_C_CHANNEL_FIXED;
  creator.item_size = sizeof(std::uint32_t);
  creator.schema_id = 0x1234ull;
  creator.creator_timestamp_ns = 0x1122334455667788ull;
  creator.creator_flags = 0x8877665544332211ull;

  xproc_c_producer* producer = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&creator, &producer), XPROC_C_STATUS_OK);

  xproc_c_options inferred{};
  ASSERT_EQ(xproc_c_shm_read_existing_options(path.c_str(), "Local", &inferred), XPROC_C_STATUS_OK);
  EXPECT_EQ(inferred.channel_type, XPROC_C_CHANNEL_FIXED);
  EXPECT_EQ(inferred.item_size, sizeof(std::uint32_t));
  EXPECT_EQ(inferred.schema_id, 0x1234ull);
  EXPECT_EQ(inferred.creator_timestamp_ns, 0x1122334455667788ull);
  EXPECT_EQ(inferred.creator_flags, 0x8877665544332211ull);
  EXPECT_EQ(inferred.create_if_missing, 0);

  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}
```

- [ ] **Step 2: Run the focused C API test and confirm it fails**

Run:

```bash
cmake --build build --target xproc_capi_smoke_tests
ctest --test-dir build --output-on-failure -R "CApiSmoke[.]ReadExistingShmOptionsInfersManifestBackedFields"
```

Expected:
- build or link failure because `xproc_c_shm_read_existing_options` does not exist yet

- [ ] **Step 3: Add the C API declaration and implementation**

Add the public declaration in `capi/xproc_c.h`:

```c
XPROC_C_API xproc_c_status xproc_c_shm_read_existing_options(const char* path,
                                                             const char* win32_object_namespace,
                                                             xproc_c_options* out_options);
```

Implement it in `capi/xproc_c.cpp` by reusing `xproc::ipc::detail::read_existing_shm_options(...)`, translating the result through `fill_borrowed_options(...)`, and keeping the current thread-local error behavior:

```cpp
#include <xproc/ipc/shm_builders.hpp>

xproc_c_status xproc_c_shm_read_existing_options(const char* path, const char* win32_object_namespace,
                                                 xproc_c_options* out_options) {
  if (path == nullptr) {
    return invalid_argument("xproc_c_shm_read_existing_options: path must not be null");
  }
  if (out_options == nullptr) {
    return invalid_argument("xproc_c_shm_read_existing_options: out_options must not be null");
  }

  return catch_status([&]() -> xproc_c_status {
    const std::string ns = (win32_object_namespace != nullptr) ? win32_object_namespace : "Local";
    const xproc::ipc::transport_options options =
        xproc::ipc::detail::read_existing_shm_options(path, ns, "xproc_c_shm_read_existing_options: ");
    xproc_c_options_init(out_options);
    fill_borrowed_options(options, out_options);
    clear_last_error();
    return XPROC_C_STATUS_OK;
  });
}
```

- [ ] **Step 4: Rebuild and rerun the focused test**

Run:

```bash
cmake --build build --target xproc_capi_smoke_tests
ctest --test-dir build --output-on-failure -R "CApiSmoke[.]ReadExistingShmOptionsInfersManifestBackedFields"
```

Expected:
- build succeeds
- the new C API smoke test passes

- [ ] **Step 5: Commit**

```bash
git add capi/xproc_c.h capi/xproc_c.cpp tests/capi_smoke_test.cpp
git commit -m "feat: add capi helper for existing shm options"
```

### Task 2: Add High-Level Shared-Memory APIs

**Files:**
- Create: `node/test/high_level_shm.test.ts`
- Modify: `node/src/node_binding.cpp`
- Modify: `node/index.js`
- Modify: `node/index.d.ts`
- Reference: `node/test/smoke.test.ts`

- [ ] **Step 1: Write failing high-level SHM tests**

Create `node/test/high_level_shm.test.ts` with coverage for fixed create/attach, varlen create/attach, and schema mismatch without repeating `itemSize`.

```ts
const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");

const xproc = require("../index.js") as XprocModule;

let shmSequence = 0;

function uniqueShmPath(label: string): string {
  shmSequence += 1;
  return `/xproc_node_high_level_${process.pid}_${Date.now()}_${shmSequence}_${label}`;
}

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // ignore best-effort cleanup failures
  }
}

test("high-level shm fixed create/attach infers manifest fields", () => {
  const path = uniqueShmPath("fixed");
  cleanupShm(path);

  const created = xproc.shm.createFixedChannel({
    path,
    itemSize: 4,
    dataCapacity: 4096n,
    schemaId: 0x1234n,
    creatorTimestampNs: 0x1122334455667788n,
    creatorFlags: 0x8877665544332211n,
  });

  const producer = created.openProducer();
  const consumer = xproc.shm.attachFixedChannel({ path, schemaId: 0x1234n }).openConsumer();
  const observer = xproc.shm.attachFixedChannel({ path, schemaId: 0x1234n }).openObserver();

  try {
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));
    const observed = observer.peekCopy();
    const polled = consumer.pollCopy();
    assert.ok(observed !== null);
    assert.ok(polled !== null);
    assert.deepEqual([...Buffer.from(observed!)], [1, 2, 3, 4]);
    assert.deepEqual([...Buffer.from(polled!)], [1, 2, 3, 4]);
    assert.equal(consumer.options().itemSize, 4);
    assert.equal(observer.options().schemaId, 0x1234n);
  } finally {
    observer.close();
    consumer.close();
    producer.close();
    cleanupShm(path);
  }
});

test("high-level shm attach mismatch surfaces layout metadata without itemSize", () => {
  const path = uniqueShmPath("schema_mismatch");
  cleanupShm(path);
  const created = xproc.shm.createFixedChannel({
    path,
    itemSize: 4,
    dataCapacity: 4096n,
    schemaId: 7n,
  });
  const producer = created.openProducer();

  try {
    assert.throws(
      () => xproc.shm.attachFixedChannel({ path, schemaId: 8n }).openConsumer(),
      (error: unknown) => {
        assert.ok(error instanceof Error);
        const xprocError = error as Error & { layoutError?: number };
        assert.equal(xprocError.layoutError, xproc.LAYOUT_ERROR.schemaIdMismatch);
        assert.match(xprocError.message, /schema_id mismatch/i);
        return true;
      },
    );
  } finally {
    producer.close();
    cleanupShm(path);
  }
});
```

- [ ] **Step 2: Run the focused Node test and confirm it fails**

Run:

```bash
cd node
node --test --experimental-strip-types test/high_level_shm.test.ts
```

Expected:
- FAIL because `xproc.shm` does not exist yet

- [ ] **Step 3: Export an internal native helper and implement the SHM namespace**

Expose a non-public native binding helper in `node/src/node_binding.cpp`:

```cpp
Napi::Value read_existing_shm_options_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    throw_type_error(env, "_readExistingShmOptions requires a path string");
    return env.Undefined();
  }

  const std::string path = info[0].As<Napi::String>().Utf8Value();
  const char* win32_ns = nullptr;
  std::string storage;
  if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
    if (!info[1].IsString()) {
      throw_type_error(env, "_readExistingShmOptions win32ObjectNamespace must be a string when provided");
      return env.Undefined();
    }
    storage = info[1].As<Napi::String>().Utf8Value();
    win32_ns = storage.c_str();
  }

  xproc_c_options options{};
  const xproc_c_status status = xproc_c_shm_read_existing_options(path.c_str(), win32_ns, &options);
  if (status != XPROC_C_STATUS_OK) {
    throw_xproc_status(env, status, "_readExistingShmOptions");
    return env.Undefined();
  }
  return options_to_js(env, options);
}
```

Register it in `Init(...)`:

```cpp
exports.Set("_readExistingShmOptions", Napi::Function::New(env, read_existing_shm_options_callback));
```

Then extend `node/index.js` to keep the helper internal and add the public `shm` namespace:

```js
const nativeModule = loadNative();
const { _readExistingShmOptions, ...native } = nativeModule;

function requirePath(value, fieldName) {
  if (typeof value !== "string" || value.length === 0) {
    throw new TypeError(`${fieldName} must be a non-empty string`);
  }
  return value;
}

function inferExistingShmOptions(pathValue, win32ObjectNamespace) {
  return decorateOptions(_readExistingShmOptions(pathValue, win32ObjectNamespace));
}

function makeShmEndpoints(baseOptions) {
  return Object.freeze({
    options() {
      return { ...baseOptions };
    },
    openProducer() {
      return new Producer(baseOptions);
    },
    openConsumer() {
      return new Consumer(baseOptions);
    },
    openObserver() {
      return new Observer({ ...baseOptions, createIfMissing: false });
    },
  });
}

const shm = Object.freeze({
  createFixedChannel(options) {
    const path = requirePath(options?.path, "path");
    const itemSize = normalizeIntegerLike(options?.itemSize, "itemSize");
    const dataCapacity = normalizeIntegerLike(options?.dataCapacity, "dataCapacity");
    const baseOptions = normalizeOptions({
      path,
      shmSize: shmSizeForDataCapacity(dataCapacity),
      itemSize,
      dataAlign: options?.dataAlign,
      schemaId: options?.schemaId,
      creatorTimestampNs: options?.creatorTimestampNs,
      creatorFlags: options?.creatorFlags,
      win32ObjectNamespace: options?.win32ObjectNamespace,
      createIfMissing: true,
      channelType: CHANNEL_TYPE.fixed,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeShmEndpoints(baseOptions);
  },
  attachFixedChannel(options) {
    const path = requirePath(options?.path, "path");
    const inferred = inferExistingShmOptions(path, options?.win32ObjectNamespace);
    if (inferred.channelType !== CHANNEL_TYPE.fixed) {
      throw new Error("attachFixedChannel: expected a fixed channel");
    }
    if (options?.schemaId !== undefined && inferred.schemaId !== BigInt(options.schemaId)) {
      const consumer = new Consumer({
        path,
        shmSize: XPROC_C_INFER_EXISTING_SHM_SIZE,
        createIfMissing: false,
        channelType: "fixed",
        itemSize: inferred.itemSize,
        schemaId: options.schemaId,
        win32ObjectNamespace: options?.win32ObjectNamespace,
      });
      consumer.close();
    }
    return makeShmEndpoints({ ...inferred, createIfMissing: false });
  },
  createVarlenChannel(options) {
    const path = requirePath(options?.path, "path");
    const dataCapacity = normalizeIntegerLike(options?.dataCapacity, "dataCapacity");
    const baseOptions = normalizeOptions({
      path,
      shmSize: shmSizeForDataCapacity(dataCapacity),
      dataAlign: options?.dataAlign,
      schemaId: options?.schemaId,
      creatorTimestampNs: options?.creatorTimestampNs,
      creatorFlags: options?.creatorFlags,
      win32ObjectNamespace: options?.win32ObjectNamespace,
      createIfMissing: true,
      channelType: CHANNEL_TYPE.varlen,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeShmEndpoints(baseOptions);
  },
  attachVarlenChannel(options) {
    const path = requirePath(options?.path, "path");
    const inferred = inferExistingShmOptions(path, options?.win32ObjectNamespace);
    if (inferred.channelType !== CHANNEL_TYPE.varlen) {
      throw new Error("attachVarlenChannel: expected a varlen channel");
    }
    return makeShmEndpoints({ ...inferred, createIfMissing: false });
  },
});
```

Extend `node/index.d.ts` with explicit high-level types:

```ts
  interface ShmFixedCreateOptions {
    path: string;
    itemSize: IntegerLike;
    dataCapacity: IntegerLike;
    dataAlign?: IntegerLike;
    schemaId?: IntegerLike;
    creatorTimestampNs?: IntegerLike;
    creatorFlags?: IntegerLike;
    win32ObjectNamespace?: string | null;
  }

  interface ShmVarlenCreateOptions {
    path: string;
    dataCapacity: IntegerLike;
    dataAlign?: IntegerLike;
    schemaId?: IntegerLike;
    creatorTimestampNs?: IntegerLike;
    creatorFlags?: IntegerLike;
    win32ObjectNamespace?: string | null;
  }

  interface ShmAttachOptions {
    path: string;
    schemaId?: IntegerLike;
    win32ObjectNamespace?: string | null;
  }

  interface ShmChannelEndpoints {
    options(): ResolvedTransportOptions;
    openProducer(): Producer;
    openConsumer(): Consumer;
    openObserver(): Observer;
  }

  interface ShmNamespace {
    createFixedChannel(options: ShmFixedCreateOptions): ShmChannelEndpoints;
    attachFixedChannel(options: ShmAttachOptions): ShmChannelEndpoints;
    createVarlenChannel(options: ShmVarlenCreateOptions): ShmChannelEndpoints;
    attachVarlenChannel(options: ShmAttachOptions): ShmChannelEndpoints;
  }

  const shm: ShmNamespace;
```

Export it from `module.exports`:

```js
module.exports = {
  ...native,
  Producer,
  Consumer,
  Observer,
  STATUS,
  ENDPOINT_KIND,
  BACKEND,
  CHANNEL_TYPE,
  LAYOUT_ERROR,
  shm,
  shmSizeForDataCapacity,
  shmDataCapacityForSize,
  validateOptionsFor,
};
```

- [ ] **Step 4: Rebuild the addon and rerun the focused SHM tests**

Run:

```bash
cmake --build build --target xproc_node
cd node
node --test --experimental-strip-types test/high_level_shm.test.ts
```

Expected:
- PASS for the fixed/varlen SHM happy path and schema mismatch checks

- [ ] **Step 5: Commit**

```bash
git add node/src/node_binding.cpp node/index.js node/index.d.ts node/test/high_level_shm.test.ts
git commit -m "feat: add high-level shm node api"
```

### Task 3: Add High-Level Socket APIs

**Files:**
- Create: `node/test/high_level_socket.test.ts`
- Modify: `node/index.js`
- Modify: `node/index.d.ts`

- [ ] **Step 1: Write failing high-level socket tests**

Create `node/test/high_level_socket.test.ts` with fixed and varlen loopback coverage using ephemeral listener ports.

```ts
const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");

const xproc = require("../index.js") as XprocModule;

test("high-level socket fixed listen/connect roundtrip", () => {
  const listener = xproc.socket.listenFixed({
    host: "127.0.0.1",
    port: 0,
    itemSize: 4,
  });
  const consumer = listener.openConsumer();
  const producer = xproc.socket.connectFixed({
    host: "127.0.0.1",
    port: consumer.socketPort(),
    itemSize: 4,
  }).openProducer();

  try {
    producer.sendFixedSized(Buffer.from([9, 8, 7, 6]));
    const payload = consumer.pollCopy();
    assert.ok(payload !== null);
    assert.deepEqual([...Buffer.from(payload!)], [9, 8, 7, 6]);
  } finally {
    producer.close();
    consumer.close();
  }
});

test("high-level socket varlen listen/connect roundtrip", () => {
  const listener = xproc.socket.listenVarlen({
    host: "127.0.0.1",
    port: 0,
  });
  const consumer = listener.openConsumer();
  const producer = xproc.socket.connectVarlen({
    host: "127.0.0.1",
    port: consumer.socketPort(),
  }).openProducer();

  try {
    producer.sendVarlen("hello-socket");
    const payload = consumer.pollCopy();
    assert.ok(payload !== null);
    assert.equal(Buffer.from(payload!).toString("utf8"), "hello-socket");
  } finally {
    producer.close();
    consumer.close();
  }
});
```

- [ ] **Step 2: Run the focused socket tests and confirm they fail**

Run:

```bash
cd node
node --test --experimental-strip-types test/high_level_socket.test.ts
```

Expected:
- FAIL because `xproc.socket` does not exist yet

- [ ] **Step 3: Implement the `socket` namespace and its types**

Add the JS high-level namespace:

```js
function makeSocketListener(baseOptions) {
  return Object.freeze({
    options() {
      return { ...baseOptions };
    },
    openConsumer() {
      return new Consumer(baseOptions);
    },
  });
}

function makeSocketConnector(baseOptions) {
  return Object.freeze({
    options() {
      return { ...baseOptions };
    },
    openProducer() {
      return new Producer(baseOptions);
    },
  });
}

const socket = Object.freeze({
  listenFixed(options) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.fixed,
      itemSize: options?.itemSize,
      socketHost: options?.host ?? "127.0.0.1",
      socketPort: options?.port ?? 0,
      socketListen: true,
    });
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeSocketListener(baseOptions);
  },
  connectFixed(options) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.fixed,
      itemSize: options?.itemSize,
      socketHost: options?.host,
      socketPort: options?.port,
      socketListen: false,
      socketConnectRetries: options?.connectRetries,
      socketConnectRetryMs: options?.connectRetryMs,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    return makeSocketConnector(baseOptions);
  },
  listenVarlen(options) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.varlen,
      socketHost: options?.host ?? "127.0.0.1",
      socketPort: options?.port ?? 0,
      socketListen: true,
    });
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeSocketListener(baseOptions);
  },
  connectVarlen(options) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.varlen,
      socketHost: options?.host,
      socketPort: options?.port,
      socketListen: false,
      socketConnectRetries: options?.connectRetries,
      socketConnectRetryMs: options?.connectRetryMs,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    return makeSocketConnector(baseOptions);
  },
});
```

Add matching declaration types:

```ts
  interface SocketListenFixedOptions {
    host?: string | null;
    port: IntegerLike;
    itemSize: IntegerLike;
  }

  interface SocketConnectFixedOptions {
    host: string;
    port: IntegerLike;
    itemSize: IntegerLike;
    connectRetries?: number;
    connectRetryMs?: number;
  }

  interface SocketListenVarlenOptions {
    host?: string | null;
    port: IntegerLike;
  }

  interface SocketConnectVarlenOptions {
    host: string;
    port: IntegerLike;
    connectRetries?: number;
    connectRetryMs?: number;
  }

  interface SocketListener {
    options(): ResolvedTransportOptions;
    openConsumer(): Consumer;
  }

  interface SocketConnector {
    options(): ResolvedTransportOptions;
    openProducer(): Producer;
  }

  interface SocketNamespace {
    listenFixed(options: SocketListenFixedOptions): SocketListener;
    connectFixed(options: SocketConnectFixedOptions): SocketConnector;
    listenVarlen(options: SocketListenVarlenOptions): SocketListener;
    connectVarlen(options: SocketConnectVarlenOptions): SocketConnector;
  }

  const socket: SocketNamespace;
```

Export the namespace:

```js
module.exports = {
  ...native,
  Producer,
  Consumer,
  Observer,
  STATUS,
  ENDPOINT_KIND,
  BACKEND,
  CHANNEL_TYPE,
  LAYOUT_ERROR,
  shm,
  socket,
  shmSizeForDataCapacity,
  shmDataCapacityForSize,
  validateOptionsFor,
};
```

- [ ] **Step 4: Run the focused socket tests**

Run:

```bash
cd node
node --test --experimental-strip-types test/high_level_socket.test.ts
```

Expected:
- PASS for both fixed and varlen socket loopback tests

- [ ] **Step 5: Commit**

```bash
git add node/index.js node/index.d.ts node/test/high_level_socket.test.ts
git commit -m "feat: add high-level socket node api"
```

### Task 4: Make the Scoped Package Packable and Loadable from Bundled Prebuilds

**Files:**
- Create: `node/lib/load-native.js`
- Create: `node/scripts/stage-prebuild.js`
- Create: `node/scripts/assert-packlist.js`
- Create: `node/scripts/verify-packed-install.js`
- Modify: `node/index.js`
- Modify: `node/package.json`
- Modify: `node/package-lock.json`
- Modify: `node/.gitignore`

- [ ] **Step 1: Write failing packlist and clean-room install checks**

Create `node/scripts/assert-packlist.js`:

```js
"use strict";

const assert = require("node:assert/strict");
const childProcess = require("node:child_process");

const raw = childProcess.execFileSync("npm", ["pack", "--dry-run", "--json"], {
  cwd: __dirname + "/..",
  encoding: "utf8",
});
const [packResult] = JSON.parse(raw);
const files = new Set(packResult.files.map((entry) => entry.path));

assert.equal(packResult.name, "@merlot/xproc");
assert.ok([...files].some((file) => file.startsWith("prebuilds/")), "expected packaged prebuilds/");
assert.ok(files.has("index.js"));
assert.ok(files.has("index.d.ts"));
assert.ok(files.has("README.md"));
assert.ok(![...files].some((file) => file.startsWith("src/")), "did not expect native source files");
assert.ok(![...files].some((file) => file.startsWith("test/")), "did not expect node tests");
assert.ok(![...files].some((file) => file.startsWith("tsconfig")), "did not expect tsconfig files");
```

Create `node/scripts/verify-packed-install.js`:

```js
"use strict";

const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const childProcess = require("node:child_process");

const packageDir = path.resolve(__dirname, "..");
const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), "xproc-pack-"));
const packOutput = childProcess.execFileSync("npm", ["pack", "--json"], {
  cwd: packageDir,
  encoding: "utf8",
});
const [{ filename }] = JSON.parse(packOutput);
const tarballPath = path.join(packageDir, filename);
const projectDir = path.join(tempRoot, "project");
fs.mkdirSync(projectDir, { recursive: true });

childProcess.execFileSync("npm", ["init", "-y"], { cwd: projectDir, stdio: "ignore" });
childProcess.execFileSync("npm", ["install", tarballPath], { cwd: projectDir, stdio: "inherit" });
childProcess.execFileSync(
  "node",
  [
    "-e",
    `
const xproc = require("@merlot/xproc");
const path = "/xproc_pack_verify_" + process.pid;
try { xproc.shmUnlink(path); } catch {}
const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity: 4096n });
const producer = created.openProducer();
const consumer = xproc.shm.attachFixedChannel({ path }).openConsumer();
producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));
const payload = consumer.pollCopy();
if (!payload) throw new Error("expected payload after packaged install");
consumer.close();
producer.close();
try { xproc.shmUnlink(path); } catch {}
`,
  ],
  { cwd: projectDir, stdio: "inherit" },
);
```

Run:

```bash
cd node
node scripts/assert-packlist.js
node scripts/verify-packed-install.js
```

Expected:
- `assert-packlist.js` fails because the package is still named `xproc` and does not include `prebuilds/`
- `verify-packed-install.js` fails because the packed tarball still cannot `require("@merlot/xproc")`

- [ ] **Step 2: Implement the scoped package metadata, loader, and staging script**

Create `node/lib/load-native.js`:

```js
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const load = require("node-gyp-build");

function tryRequire(candidate) {
  if (!fs.existsSync(candidate)) {
    return null;
  }
  return require(candidate);
}

module.exports = function loadNative(rootDir) {
  try {
    return load(rootDir);
  } catch (packagedError) {
    const buildDir = path.join(rootDir, "..", "build");
    const candidates = [
      path.join(rootDir, "xproc.node"),
      path.join(buildDir, "node", "xproc.node"),
      path.join(buildDir, "node", "Release", "xproc.node"),
      path.join(buildDir, "node", "Debug", "xproc.node"),
      path.join(buildDir, "node", "RelWithDebInfo", "xproc.node"),
      path.join(buildDir, "node", "MinSizeRel", "xproc.node"),
      path.join(buildDir, "Release", "xproc.node"),
      path.join(buildDir, "Debug", "xproc.node"),
    ];

    let lastError = packagedError;
    for (const candidate of candidates) {
      try {
        const loaded = tryRequire(candidate);
        if (loaded !== null) {
          return loaded;
        }
      } catch (error) {
        lastError = error;
      }
    }

    throw new Error(
      [
        "Unable to load xproc native addon.",
        "Tried packaged prebuilds and local CMake build outputs.",
        `Last error: ${lastError instanceof Error ? lastError.message : String(lastError)}`,
      ].join(" "),
    );
  }
};
```

Update `node/index.js` to import the loader:

```js
const loadNative = require("./lib/load-native");
const nativeModule = loadNative(__dirname);
const { _readExistingShmOptions, ...native } = nativeModule;
```

Update `node/package.json`:

```json
{
  "name": "@merlot/xproc",
  "version": "0.1.0",
  "main": "index.js",
  "types": "index.d.ts",
  "files": [
    "index.js",
    "index.d.ts",
    "globals.d.ts",
    "xproc-types.d.ts",
    "lib/",
    "examples/",
    "README.md",
    "LICENSE",
    "prebuilds/"
  ],
  "scripts": {
    "test": "node --test --experimental-strip-types test/**/*.test.ts",
    "typecheck": "tsc -p tsconfig.typecheck.json",
    "stage:prebuild": "node scripts/stage-prebuild.js",
    "assert:packlist": "node scripts/assert-packlist.js",
    "verify:pack": "node scripts/verify-packed-install.js"
  },
  "dependencies": {
    "node-addon-api": "^8.7.0",
    "node-api-headers": "^1.8.0",
    "node-gyp-build": "^4.8.4"
  },
  "publishConfig": {
    "access": "public"
  },
  "repository": {
    "type": "git",
    "url": "git+https://github.com/merlotqi/xproc.git",
    "directory": "node"
  }
}
```

Create `node/scripts/stage-prebuild.js`:

```js
"use strict";

const fs = require("node:fs");
const path = require("node:path");

function readArg(flag) {
  const index = process.argv.indexOf(flag);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

const source = path.resolve(readArg("--source") ?? path.join(__dirname, "..", "..", "build", "node", "xproc.node"));
const platform = readArg("--platform") ?? process.platform;
const arch = readArg("--arch") ?? process.arch;
const libc = readArg("--libc") ?? (platform === "linux" ? "glibc" : "");

const prebuildDir = path.join(__dirname, "..", "prebuilds", `${platform}-${arch}`);
const filename = platform === "linux" && libc === "glibc" ? "node.napi.glibc.node" : "node.napi.node";
const destination = path.join(prebuildDir, filename);

fs.mkdirSync(prebuildDir, { recursive: true });
fs.copyFileSync(source, destination);
console.log(destination);
```

Create `node/LICENSE` by copying the repository root license text verbatim into the package directory so `files` can include it without crossing outside the package root.

Update `node/.gitignore`:

```gitignore
node_modules
build
prebuilds
*.tgz
```

Refresh the lockfile with:

```bash
cp LICENSE node/LICENSE
cd node
npm install node-gyp-build --save
```

- [ ] **Step 3: Build, stage the host prebuild, and rerun the packaging checks**

Run:

```bash
cmake --build build --target xproc_node
cd node
node scripts/stage-prebuild.js --source ../build/node/xproc.node
node scripts/assert-packlist.js
node scripts/verify-packed-install.js
```

Expected:
- the host prebuild is copied into `node/prebuilds/<platform>-<arch>/`
- the dry-run packlist passes
- the clean-room install succeeds and the installed package can run a high-level SHM roundtrip

- [ ] **Step 4: Commit**

```bash
git add node/LICENSE node/lib/load-native.js node/scripts/stage-prebuild.js node/scripts/assert-packlist.js node/scripts/verify-packed-install.js node/index.js node/package.json node/package-lock.json node/.gitignore
git commit -m "feat: package scoped node prebuilds for npm"
```

### Task 5: Update Examples and README to the New Happy-Path APIs

**Files:**
- Create: `node/examples/socket_fixed_loopback.ts`
- Create: `node/examples/socket_varlen_loopback.ts`
- Modify: `node/examples/fixed_channel_inprocess.ts`
- Modify: `node/examples/varlen_channel_inprocess.ts`
- Modify: `node/examples/observer_peek_demo.ts`
- Modify: `node/package.json`
- Modify: `node/README.md`

- [ ] **Step 1: Update the shared-memory examples to use `xproc.shm`**

Convert the in-process examples so they stop hand-writing `TransportOptions` for the common path.

Example replacement for `node/examples/fixed_channel_inprocess.ts`:

```ts
const created = xproc.shm.createFixedChannel({
  path: shmPath,
  itemSize: 4,
  dataCapacity: 16384n,
});

const producer = created.openProducer();
const consumer = xproc.shm.attachFixedChannel({ path: shmPath }).openConsumer();
```

Example replacement for `node/examples/varlen_channel_inprocess.ts`:

```ts
const created = xproc.shm.createVarlenChannel({
  path: shmPath,
  dataCapacity: 32768n,
});

const producer = created.openProducer();
const consumer = xproc.shm.attachVarlenChannel({ path: shmPath }).openConsumer();
```

Example replacement for `node/examples/observer_peek_demo.ts`:

```ts
const created = xproc.shm.createFixedChannel({
  path: shmPath,
  itemSize: 4,
  dataCapacity: 16384n,
});

const producer = created.openProducer();
const consumer = xproc.shm.attachFixedChannel({ path: shmPath }).openConsumer();
const observer = xproc.shm.attachFixedChannel({ path: shmPath }).openObserver();
```

- [ ] **Step 2: Add socket loopback examples and wire package scripts**

Create `node/examples/socket_fixed_loopback.ts`:

```ts
const xproc = require("../index.js") as XprocModule;

const listener = xproc.socket.listenFixed({
  host: "127.0.0.1",
  port: 0,
  itemSize: 4,
});

const consumer = listener.openConsumer();
const producer = xproc.socket.connectFixed({
  host: "127.0.0.1",
  port: consumer.socketPort(),
  itemSize: 4,
}).openProducer();

producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));
console.log(Buffer.from(consumer.pollCopy() ?? []).readInt32LE(0));
producer.close();
consumer.close();
```

Create `node/examples/socket_varlen_loopback.ts`:

```ts
const xproc = require("../index.js") as XprocModule;

const listener = xproc.socket.listenVarlen({
  host: "127.0.0.1",
  port: 0,
});

const consumer = listener.openConsumer();
const producer = xproc.socket.connectVarlen({
  host: "127.0.0.1",
  port: consumer.socketPort(),
}).openProducer();

producer.sendVarlen("hello-varlen-socket");
console.log(Buffer.from(consumer.pollCopy() ?? []).toString("utf8"));
producer.close();
consumer.close();
```

Add package scripts:

```json
"example:socket-fixed-loopback": "node --experimental-strip-types examples/socket_fixed_loopback.ts",
"example:socket-varlen-loopback": "node --experimental-strip-types examples/socket_varlen_loopback.ts"
```

- [ ] **Step 3: Rewrite `node/README.md` from an npm-consumer perspective**

Update the README so the first sections are:

```md
# @merlot/xproc

Install:

```bash
npm install @merlot/xproc
```

Supported prebuilt targets:

- Linux x64 glibc
- Linux arm64 glibc
- macOS x64
- macOS arm64
- Windows x64

Shared-memory quick start:

```ts
const xproc = require("@merlot/xproc");

const created = xproc.shm.createFixedChannel({
  path: "/demo",
  itemSize: 4,
  dataCapacity: 16384n,
});

const producer = created.openProducer();
const consumer = xproc.shm.attachFixedChannel({ path: "/demo" }).openConsumer();
```
```

Keep a later “Advanced / raw API” section for `new Producer(...)` / `new Consumer(...)` / `new Observer(...)`.

- [ ] **Step 4: Typecheck and run representative examples**

Run:

```bash
cd node
npm run typecheck
npm run example:fixed-channel-inprocess
npm run example:socket-fixed-loopback
```

Expected:
- typecheck passes
- both example scripts run without errors

- [ ] **Step 5: Commit**

```bash
git add node/examples/fixed_channel_inprocess.ts node/examples/varlen_channel_inprocess.ts node/examples/observer_peek_demo.ts node/examples/socket_fixed_loopback.ts node/examples/socket_varlen_loopback.ts node/package.json node/README.md
git commit -m "docs: move node examples to high-level api"
```

### Task 6: Add CI Packaging Checks and a Publish Workflow

**Files:**
- Modify: `.github/workflows/ci.yml`
- Create: `.github/workflows/node-package-publish.yml`
- Reference: `node/scripts/stage-prebuild.js`
- Reference: `node/scripts/assert-packlist.js`
- Reference: `node/scripts/verify-packed-install.js`

- [ ] **Step 1: Extend regular CI with Node typecheck and host-platform pack verification**

Add a Linux-only post-build packaging check to `.github/workflows/ci.yml`:

```yaml
      - name: Node typecheck
        working-directory: node
        run: npm run typecheck

      - name: Stage host prebuild
        working-directory: node
        run: node scripts/stage-prebuild.js --source ../build/node/xproc.node

      - name: Verify package contents
        working-directory: node
        run: node scripts/assert-packlist.js

      - name: Verify clean-room package install
        working-directory: node
        run: node scripts/verify-packed-install.js
```

Keep the existing CMake/CTest coverage; this CI change adds an early signal for package regressions.

- [ ] **Step 2: Add the dedicated prebuild-and-publish workflow**

Create `.github/workflows/node-package-publish.yml`:

```yaml
name: Node Package Publish

on:
  workflow_dispatch:
  push:
    tags:
      - "node-v*"

permissions:
  contents: read

jobs:
  build-prebuild:
    strategy:
      fail-fast: false
      matrix:
        include:
          - runner: ubuntu-24.04
            platform: linux
            arch: x64
            libc: glibc
            source: build/node/xproc.node
          - runner: ubuntu-24.04-arm
            platform: linux
            arch: arm64
            libc: glibc
            source: build/node/xproc.node
          - runner: macos-15-intel
            platform: darwin
            arch: x64
            libc: ""
            source: build/node/xproc.node
          - runner: macos-14
            platform: darwin
            arch: arm64
            libc: ""
            source: build/node/xproc.node
          - runner: windows-latest
            platform: win32
            arch: x64
            libc: ""
            source: build/node/Release/xproc.node
    runs-on: ${{ matrix.runner }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - uses: actions/setup-node@v4
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: node/package-lock.json
      - name: Install Node deps
        working-directory: node
        run: npm ci
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXPROC_BUILD_NODE=ON -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_TESTS=OFF -DXPROC_BUILD_EXAMPLES=OFF -DXPROC_BUILD_PYTHON=OFF -DXPROC_BUILD_BENCHMARKS=OFF
      - name: Build addon
        run: cmake --build build --config Release --target xproc_node
      - name: Stage prebuild
        working-directory: node
        run: node scripts/stage-prebuild.js --source ../${{ matrix.source }} --platform ${{ matrix.platform }} --arch ${{ matrix.arch }} --libc ${{ matrix.libc }}
      - uses: actions/upload-artifact@v4
        with:
          name: prebuild-${{ matrix.platform }}-${{ matrix.arch }}
          path: node/prebuilds/

  publish:
    needs: build-prebuild
    runs-on: ubuntu-latest
    permissions:
      contents: read
      id-token: write
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - uses: actions/setup-node@v4
        with:
          node-version: 24
          registry-url: https://registry.npmjs.org
          cache: npm
          cache-dependency-path: node/package-lock.json
      - uses: actions/download-artifact@v4
        with:
          pattern: prebuild-*
          path: node/prebuilds
          merge-multiple: true
      - name: Install Node deps
        working-directory: node
        run: npm ci
      - name: Verify package contents
        working-directory: node
        run: node scripts/assert-packlist.js
      - name: Verify clean-room package install
        working-directory: node
        run: node scripts/verify-packed-install.js
      - name: Publish
        working-directory: node
        run: npm publish
```

- [ ] **Step 3: Re-run the same local package-verification commands the workflows depend on**

Run:

```bash
cd node
node scripts/assert-packlist.js
node scripts/verify-packed-install.js
```

Expected:
- both commands pass locally on the current platform
- the workflow is only adding automation around commands already proven locally

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml .github/workflows/node-package-publish.yml
git commit -m "ci: add node prebuild packaging workflow"
```

### Task 7: Full Verification and Release Handoff

**Files:**
- Reference: `node/package.json`
- Reference: `.github/workflows/node-package-publish.yml`
- Reference: `node/scripts/assert-packlist.js`
- Reference: `node/scripts/verify-packed-install.js`
- Reference: `docs/superpowers/specs/2026-04-26-node-api-npm-publishing-design.md`

- [ ] **Step 1: Rebuild the updated targets**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXPROC_BUILD_TESTS=ON -DXPROC_BUILD_EXAMPLES=ON -DXPROC_BUILD_NODE=ON
cmake --build build --parallel
```

Expected:
- the C API helper, Node addon, and tests all rebuild from current sources

- [ ] **Step 2: Run the full verification set**

Run:

```bash
cd node
npm run typecheck
npm test
node scripts/stage-prebuild.js --source ../build/node/xproc.node
node scripts/assert-packlist.js
node scripts/verify-packed-install.js
cd ..
ctest --test-dir build --output-on-failure
```

Expected:
- typecheck passes
- all Node tests pass, including raw and high-level coverage
- the staged host prebuild verifies in both dry-run packlist and clean-room install
- the full CTest suite passes

- [ ] **Step 3: Configure publish prerequisites for `@merlot/xproc`**

Do these release-prep actions before the first real publish:

- Ensure `node/package.json` still contains:

```json
"name": "@merlot/xproc",
"publishConfig": {
  "access": "public"
},
"repository": {
  "type": "git",
  "url": "git+https://github.com/merlotqi/xproc.git",
  "directory": "node"
}
```

- In npm package settings, configure trusted publishing for:
  - Package: `@merlot/xproc`
  - GitHub owner: `merlotqi`
  - Repository: `xproc`
  - Workflow file: `.github/workflows/node-package-publish.yml`
- Keep the workflow on GitHub-hosted runners only.
- Trigger the first publish via a protected `node-v*` tag or `workflow_dispatch`.

- [ ] **Step 4: Record the post-implementation outcome**

In the completion note, explicitly confirm:

- the high-level SHM and socket APIs shipped
- the raw constructor APIs remained intact
- the package name is `@merlot/xproc`
- `npm install @merlot/xproc` works on the verified host platform through bundled prebuilds
- the release workflow now owns multi-platform prebuild assembly and publish

## Success Criteria

- `node/test/high_level_shm.test.ts` and `node/test/high_level_socket.test.ts` pass.
- A new C API helper makes manifest-backed SHM attach inference possible without forcing callers to repeat `itemSize`.
- `node/lib/load-native.js` prefers packaged prebuilds and still supports repository CMake builds.
- `node/package.json` is scoped as `@merlot/xproc` and includes explicit public publish settings.
- `node/scripts/assert-packlist.js` and `node/scripts/verify-packed-install.js` pass on the host platform.
- The Node README leads with `npm install @merlot/xproc` and high-level API examples.
- CI runs host-platform package verification and a dedicated publish workflow assembles all approved mainstream prebuilds.
