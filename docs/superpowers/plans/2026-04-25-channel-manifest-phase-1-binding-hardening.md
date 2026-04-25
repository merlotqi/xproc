# Channel Manifest Phase 1 Binding Hardening Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the actionable binding follow-up work identified by the Phase 1 audit without mixing in unresolved product-scope decisions.

**Architecture:** Keep the Phase 1 C++ manifest and builder work unchanged, then harden the binding surfaces around it. Specifically, wire the existing Node smoke coverage into repository automation, and strengthen the Python mismatch path so manifest failures are both tested and observable through structured exception metadata.

**Tech Stack:** CMake/CTest, GitHub Actions, Node.js test runner, pybind11, Python smoke tests, xproc C/Node/Python bindings.

---

## Scope

This plan intentionally implements only the audit items that are already concrete engineering work:

- Add automated execution for the existing Node smoke suite
- Include Node smoke coverage in the targeted Phase 1 regression gate
- Add Python manifest-mismatch smoke coverage
- Expose structured Python exception fields for manifest failures

This plan does **not** try to resolve the still-open product decisions from `docs/todo.md`, including:

- Whether creator timestamp / flags belong in the v1 manifest contract
- Whether full binding parity is part of the Phase 1 exit bar
- Whether future manifest incompatibility needs a separate manifest version field

## File Structure

- Create: `docs/superpowers/plans/2026-04-25-channel-manifest-phase-1-binding-hardening.md`
- Modify: `node/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `Python/src/python_binding.cpp`
- Modify: `Python/tests/smoke_test.py`
- Modify: `Python/xproc/__init__.pyi`

### Task 1: Wire Node Smoke Coverage into Repository Automation

**Files:**
- Modify: `node/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Reference: `node/test/smoke.test.ts`
- Reference: `.github/workflows/ci.yml`

- [ ] **Step 1: Register a Node smoke test in CMake**

Add a `BUILD_TESTING`-guarded `add_test(...)` in `node/CMakeLists.txt` that runs the existing TypeScript smoke suite with Node from the `node/` directory.

Expected outcome:
- `ctest --output-on-failure` includes a Node smoke test
- The test reuses `node/test/smoke.test.ts` rather than introducing a second automation path

- [ ] **Step 2: Ensure the test has the right build dependency**

Make sure the test-driving targets are built before the targeted Phase 1 suite runs, so `cmake --build build --target xproc_run_phase1_tests` remains reliable when invoked directly.

Expected outcome:
- The Node addon exists before the smoke test executes
- The targeted Phase 1 gate remains a one-command workflow

- [ ] **Step 3: Add the Node smoke test to the Phase 1 regression filter**

Extend the named Phase 1 regression suite so the existing JavaScript schema-mismatch check participates in the same gate the audit references.

Expected outcome:
- The named Phase 1 gate now covers the shipped Node mismatch surface
- CI gains an automated signal for JavaScript layout-error regressions

### Task 2: Harden Python Manifest Failure Observability

**Files:**
- Modify: `Python/src/python_binding.cpp`
- Modify: `Python/tests/smoke_test.py`
- Modify: `Python/xproc/__init__.pyi`

- [ ] **Step 1: Expose structured fields on Python binding errors**

Replace the bare `register_exception` usage with a binding setup that preserves the existing `XprocError` type while attaching structured `status` and `layout_error` attributes for `status_error` failures.

Expected outcome:
- Python callers can inspect both the transport-level status and manifest-specific layout error
- Existing `RuntimeError` compatibility is preserved through `XprocError`

- [ ] **Step 2: Add a Python manifest-mismatch smoke case**

Extend `Python/tests/smoke_test.py` with a schema-mismatch attach scenario that asserts:

- `XprocError` is raised
- `status == Status.LAYOUT_ERROR`
- `layout_error == LayoutError.SCHEMA_ID_MISMATCH`
- the message contains the schema mismatch text

Expected outcome:
- The current Python smoke test covers both the happy path and a manifest failure path
- The audit’s Python mismatch-coverage gap is closed with executable evidence

- [ ] **Step 3: Update the Python stub file**

Document the new structured exception fields in `Python/xproc/__init__.pyi` so the public type surface matches the runtime behavior.

Expected outcome:
- Static type information and runtime behavior stay aligned

### Task 3: Verify the Hardened Binding Paths

**Files:**
- Reference: `node/test/smoke.test.ts`
- Reference: `Python/tests/smoke_test.py`
- Reference: `tests/CMakeLists.txt`

- [ ] **Step 1: Reconfigure or rebuild the necessary targets**

Run the minimum build commands needed for updated CMake test registration and binding code.

- [ ] **Step 2: Run focused binding verification**

At minimum, run:

- `ctest --test-dir build --output-on-failure -R "xproc_node_smoke|xproc_python_smoke"`

If the named Phase 1 gate is updated to include Node smoke, also run:

- `cmake --build build --target xproc_run_phase1_tests`

- [ ] **Step 3: Record what changed and what remains open**

When reporting completion, separate:

- Implemented follow-up work now closed by code
- Remaining `docs/todo.md` items that still require product or architecture decisions

## Success Criteria

- `ai/superpowers` contains this implementation plan and no product-code edits
- `channel-manifest-phase-1` gains automated Node smoke execution
- `channel-manifest-phase-1` gains Python mismatch smoke coverage
- Python manifest failures expose structured exception metadata
- Verification demonstrates the binding hardening work is actually exercised
