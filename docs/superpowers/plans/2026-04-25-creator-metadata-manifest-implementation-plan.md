# Creator Metadata Manifest Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add persisted `creator_timestamp_ns` and `creator_flags` manifest metadata across C++, C API, Node, and Python without turning them into attach-validation requirements.

**Architecture:** Replace part of the shared-memory header reserved area with named creator-metadata fields, then thread those fields through `transport_options`, builder helpers, and all binding surfaces. Keep validation logic unchanged for compatibility: the new fields are written on create, read back on attach, and exposed for observation only.

**Tech Stack:** C++20, CMake/CTest, GoogleTest, existing SHM builder helpers, C API bridge, N-API Node binding, pybind11 Python binding, TypeScript and Python smoke tests.

---

## Scope

This plan implements:

- named control-block fields for `creator_timestamp_ns` and `creator_flags`
- `transport_options` and builder support in C++
- C API, Node, and Python exposure for the new fields
- create-to-attach readback tests and docs updates

This plan does **not** implement:

- attach mismatch validation for the new fields
- semantic meaning for individual creator flag bits
- a separate manifest metadata object
- manifest-versioning changes

## File Structure

- Create on `ai/superpowers`: `docs/superpowers/plans/2026-04-25-creator-metadata-manifest-implementation-plan.md`
- Modify on `channel-manifest-phase-1`: `include/xproc/shm/shm_layout.hpp`
- Modify on `channel-manifest-phase-1`: `include/xproc/ipc/options.hpp`
- Modify on `channel-manifest-phase-1`: `include/xproc/shm/shm_layout_manager.hpp`
- Modify on `channel-manifest-phase-1`: `include/xproc/ipc/shm_builders.hpp`
- Modify on `channel-manifest-phase-1`: `tests/layout_validate_test.cpp`
- Modify on `channel-manifest-phase-1`: `tests/api_surface_test.cpp`
- Modify on `channel-manifest-phase-1`: `capi/xproc_c.h`
- Modify on `channel-manifest-phase-1`: `capi/xproc_c.cpp`
- Modify on `channel-manifest-phase-1`: `node/src/node_binding.cpp`
- Modify on `channel-manifest-phase-1`: `node/test/smoke.test.ts`
- Modify on `channel-manifest-phase-1`: `Python/src/python_binding.cpp`
- Modify on `channel-manifest-phase-1`: `Python/xproc/__init__.pyi`
- Modify on `channel-manifest-phase-1`: `Python/tests/smoke_test.py`
- Modify on `channel-manifest-phase-1`: `README.md`

### Task 1: Add Creator Metadata to the Core Manifest and Builder Surface

**Files:**
- Modify: `include/xproc/shm/shm_layout.hpp`
- Modify: `include/xproc/ipc/options.hpp`
- Modify: `include/xproc/shm/shm_layout_manager.hpp`
- Modify: `include/xproc/ipc/shm_builders.hpp`
- Test: `tests/layout_validate_test.cpp`
- Test: `tests/api_surface_test.cpp`

- [ ] **Step 1: Write the failing C++ tests for manifest readback and builder setters**

Add coverage to `tests/layout_validate_test.cpp` and `tests/api_surface_test.cpp` along these lines:

```cpp
TEST(LayoutValidateTest, HeaderCarriesCreatorMetadata) {
  xproc::shm::control_block h{};
  h.creator_timestamp_ns = 123456789u;
  h.creator_flags = 0x55u;
  EXPECT_EQ(h.creator_timestamp_ns, 123456789u);
  EXPECT_EQ(h.creator_flags, 0x55u);
}
```

```cpp
TEST(ApiSurfaceTest, FixedBuilderCarriesCreatorMetadataIntoOptions) {
  auto opts = xproc::ipc::make_fixed_channel("/tmp/demo", 4)
                  .with_schema_id(0x1234u)
                  .with_creator_timestamp_ns(123456789u)
                  .with_creator_flags(0x55u)
                  .options_for_test();
  EXPECT_EQ(opts.creator_timestamp_ns, 123456789u);
  EXPECT_EQ(opts.creator_flags, 0x55u);
}
```

Run:

```bash
cmake --build .worktrees/channel-manifest-phase-1/build --target api_surface_test layout_validate_test --parallel
ctest --test-dir .worktrees/channel-manifest-phase-1/build --output-on-failure -R "layout_validate_test|api_surface_test"
```

Expected:
- compile or test failure because the new fields and builder setters do not exist yet

- [ ] **Step 2: Add the new fields to the control block and `transport_options`**

Update `include/xproc/shm/shm_layout.hpp`:

```cpp
  uint64_t schema_id{0};
  uint64_t creator_timestamp_ns{0};
  uint64_t creator_flags{0};
  uint64_t reserved[1];
```

Update `include/xproc/ipc/options.hpp`:

```cpp
  std::uint64_t schema_id = 0;
  std::uint64_t creator_timestamp_ns = 0;
  std::uint64_t creator_flags = 0;
```

Expected:
- core types can represent the new metadata with default `0` values

- [ ] **Step 3: Thread creator metadata through the layout-manager create path**

Update `include/xproc/shm/shm_layout_manager.hpp` so manifest initialization writes the two new values:

```cpp
  h->schema_id = schema_id;
  h->creator_timestamp_ns = creator_timestamp_ns;
  h->creator_flags = creator_flags;
```

If helper signatures currently only accept `schema_id`, extend them consistently so the create path receives:

```cpp
std::uint64_t schema_id,
std::uint64_t creator_timestamp_ns,
std::uint64_t creator_flags
```

Expected:
- creator metadata is persisted in the control block during create
- validation logic remains unchanged for attach mismatch behavior

- [ ] **Step 4: Add builder fluent setters and readback plumbing**

Update `include/xproc/ipc/shm_builders.hpp` to add:

```cpp
fixed_channel_builder& with_creator_timestamp_ns(std::uint64_t value) {
  creator_timestamp_ns_ = value;
  return *this;
}

fixed_channel_builder& with_creator_flags(std::uint64_t value) {
  creator_flags_ = value;
  return *this;
}
```

and mirror the same shape for varlen builders, with backing members:

```cpp
std::uint64_t creator_timestamp_ns_{0};
std::uint64_t creator_flags_{0};
```

Also make the create path assign:

```cpp
opts.creator_timestamp_ns = creator_timestamp_ns_;
opts.creator_flags = creator_flags_;
```

Expected:
- creator builders can populate the new fields
- attach helpers do not introduce any creator-metadata mismatch checks

- [ ] **Step 5: Run the focused C++ tests until they pass**

Run:

```bash
cmake --build .worktrees/channel-manifest-phase-1/build --target api_surface_test layout_validate_test --parallel
ctest --test-dir .worktrees/channel-manifest-phase-1/build --output-on-failure -R "layout_validate_test|api_surface_test"
```

Expected:
- the new C++ tests pass
- no new mismatch behavior appears for creator metadata

- [ ] **Step 6: Commit the core manifest and builder work**

Run:

```bash
git -C .worktrees/channel-manifest-phase-1 add \
  include/xproc/shm/shm_layout.hpp \
  include/xproc/ipc/options.hpp \
  include/xproc/shm/shm_layout_manager.hpp \
  include/xproc/ipc/shm_builders.hpp \
  tests/layout_validate_test.cpp \
  tests/api_surface_test.cpp
git -C .worktrees/channel-manifest-phase-1 commit -m "feat: add creator metadata to shm manifest"
```

### Task 2: Expose Creator Metadata Through the C API

**Files:**
- Modify: `capi/xproc_c.h`
- Modify: `capi/xproc_c.cpp`
- Test: `tests/api_surface_test.cpp`

- [ ] **Step 1: Write or extend a failing API-surface test for the C bridge**

Add a test shape like:

```cpp
TEST(ApiSurfaceTest, COptionsCarryCreatorMetadata) {
  xproc_transport_options_t c_opts{};
  c_opts.creator_timestamp_ns = 123456789u;
  c_opts.creator_flags = 0x55u;
  EXPECT_EQ(c_opts.creator_timestamp_ns, 123456789u);
  EXPECT_EQ(c_opts.creator_flags, 0x55u);
}
```

Run:

```bash
cmake --build .worktrees/channel-manifest-phase-1/build --target api_surface_test --parallel
ctest --test-dir .worktrees/channel-manifest-phase-1/build --output-on-failure -R api_surface_test
```

Expected:
- failure because the C struct or bridge code is incomplete

- [ ] **Step 2: Add the fields to the public C struct**

Update `capi/xproc_c.h`:

```c
  uint64_t schema_id;
  uint64_t creator_timestamp_ns;
  uint64_t creator_flags;
```

Keep the C field names snake_case to match the existing ABI style.

- [ ] **Step 3: Wire both conversion directions in the C bridge**

Update `capi/xproc_c.cpp` in both mapping directions:

```cpp
  out.creator_timestamp_ns = options.creator_timestamp_ns;
  out.creator_flags = options.creator_flags;
```

and:

```cpp
  out.creator_timestamp_ns = options->creator_timestamp_ns;
  out.creator_flags = options->creator_flags;
```

Also ensure reset/default helpers zero them:

```cpp
  options->creator_timestamp_ns = 0;
  options->creator_flags = 0;
```

- [ ] **Step 4: Run the focused C API test until it passes**

Run:

```bash
cmake --build .worktrees/channel-manifest-phase-1/build --target api_surface_test --parallel
ctest --test-dir .worktrees/channel-manifest-phase-1/build --output-on-failure -R api_surface_test
```

Expected:
- the API-surface test passes with the new C fields mapped correctly

- [ ] **Step 5: Commit the C API work**

Run:

```bash
git -C .worktrees/channel-manifest-phase-1 add capi/xproc_c.h capi/xproc_c.cpp tests/api_surface_test.cpp
git -C .worktrees/channel-manifest-phase-1 commit -m "feat: expose creator metadata in c api"
```

### Task 3: Expose Creator Metadata in Node and Python

**Files:**
- Modify: `node/src/node_binding.cpp`
- Modify: `node/test/smoke.test.ts`
- Modify: `Python/src/python_binding.cpp`
- Modify: `Python/xproc/__init__.pyi`
- Modify: `Python/tests/smoke_test.py`

- [ ] **Step 1: Write failing binding readback tests for Node and Python**

Extend the existing smoke tests with checks like:

```ts
assert.equal(options.creatorTimestampNs, 123456789n);
assert.equal(options.creatorFlags, 0x55n);
```

```python
assert attached.creator_timestamp_ns == 123456789
assert attached.creator_flags == 0x55
```

Run:

```bash
ctest --test-dir .worktrees/channel-manifest-phase-1/build --output-on-failure -R "xproc_node_smoke|xproc_python_smoke"
```

Expected:
- failure because the binding surfaces do not expose the new fields yet

- [ ] **Step 2: Add the Node option properties**

Update `node/src/node_binding.cpp` so option conversion and object exposure include:

```cpp
  object.Set("creatorTimestampNs", js_bigint_from_u64(env, options.creator_timestamp_ns));
  object.Set("creatorFlags", js_bigint_from_u64(env, options.creator_flags));
```

and the reverse parse path reads those properties back into `transport_options`.

- [ ] **Step 3: Add the Python fields, repr, and stub surface**

Update `Python/src/python_binding.cpp` to include:

```cpp
      .def_readwrite("creator_timestamp_ns", &transport_options::creator_timestamp_ns)
      .def_readwrite("creator_flags", &transport_options::creator_flags)
```

and extend the repr path:

```cpp
  repr += ", creator_timestamp_ns=" + std::to_string(options.creator_timestamp_ns);
  repr += ", creator_flags=" + std::to_string(options.creator_flags);
```

Update `Python/xproc/__init__.pyi` with:

```python
    creator_timestamp_ns: int
    creator_flags: int
```

- [ ] **Step 4: Run the Node and Python smoke tests until they pass**

Run:

```bash
ctest --test-dir .worktrees/channel-manifest-phase-1/build --output-on-failure -R "xproc_node_smoke|xproc_python_smoke"
```

Expected:
- both smoke tests pass
- create-to-attach readback works
- no creator-metadata mismatch error is introduced

- [ ] **Step 5: Commit the Node/Python binding work**

Run:

```bash
git -C .worktrees/channel-manifest-phase-1 add \
  node/src/node_binding.cpp \
  node/test/smoke.test.ts \
  Python/src/python_binding.cpp \
  Python/xproc/__init__.pyi \
  Python/tests/smoke_test.py
git -C .worktrees/channel-manifest-phase-1 commit -m "feat: expose creator metadata in node and python"
```

### Task 4: Document and Verify the Final Readback Contract

**Files:**
- Modify: `README.md`
- Reference: `tests/layout_validate_test.cpp`
- Reference: `node/test/smoke.test.ts`
- Reference: `Python/tests/smoke_test.py`

- [ ] **Step 1: Add a short README note that the fields are persisted metadata only**

Insert language near the shared-memory manifest section like:

```md
Creator timestamp and creator flags are persisted manifest metadata for observation and diagnostics. They are not
attach-validation requirements in the current contract.
```

- [ ] **Step 2: Run the full targeted verification gate**

Run:

```bash
cmake --build .worktrees/channel-manifest-phase-1/build --target xproc_run_phase1_tests --parallel
ctest --test-dir .worktrees/channel-manifest-phase-1/build --output-on-failure -R "layout_validate_test|api_surface_test|xproc_node_smoke|xproc_python_smoke"
```

Expected:
- the targeted Phase 1 gate stays green
- the direct creator-metadata coverage stays green

- [ ] **Step 3: Commit the doc update if it was not already included earlier**

Run:

```bash
git -C .worktrees/channel-manifest-phase-1 add README.md
git -C .worktrees/channel-manifest-phase-1 commit -m "docs: describe creator manifest metadata"
```

If `README.md` was already included in an earlier feature commit, skip the extra commit and note that in the final report.

## Success Criteria

- `shm_layout_header` stores named creator metadata fields
- `transport_options` and SHM builders expose `creator_timestamp_ns` and `creator_flags`
- C API, Node, and Python all expose the new fields
- tests prove create-to-attach readback and default `0` behavior
- no new attach-validation mismatch behavior is introduced
