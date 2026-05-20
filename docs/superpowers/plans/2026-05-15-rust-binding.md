# Rust Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a first Rust binding for xproc as a safe thin wrapper over the existing C API.

**Architecture:** Add a low-level `xproc-sys` crate that exposes the C ABI and builds against `xproc_c`, then add a higher-level `xproc` crate that owns handles safely and converts C status/error state into Rust `Result` values. Keep the first release synchronous and copy-based.

**Tech Stack:** Rust, Cargo, CMake-built `xproc_c`, C FFI

---

### Task 1: Scaffold Rust workspace

**Files:**
- Create: `rust/Cargo.toml`
- Create: `rust/xproc-sys/Cargo.toml`
- Create: `rust/xproc/Cargo.toml`

- [ ] Create a Cargo workspace for the two crates.
- [ ] Make `xproc` depend on `xproc-sys`.

### Task 2: Add failing integration test

**Files:**
- Create: `rust/xproc/tests/smoke.rs`

- [ ] Write a failing test for fixed shared-memory producer/consumer roundtrip.
- [ ] Run `cargo test -p xproc --test smoke` and confirm it fails because the API does not exist yet.

### Task 3: Implement raw FFI crate

**Files:**
- Create: `rust/xproc-sys/build.rs`
- Create: `rust/xproc-sys/src/lib.rs`

- [ ] Add opaque handle declarations, enums, structs, and extern function declarations matching `xproc_c.h`.
- [ ] Add build logic that links against the existing local CMake build output first.

### Task 4: Implement safe wrapper

**Files:**
- Create: `rust/xproc/src/lib.rs`

- [ ] Add `Options`, `Producer`, `Consumer`, `Observer`, `Snapshot`, and `XprocError`.
- [ ] Add minimal conversion and RAII behavior needed for the first smoke test.

### Task 5: Expand tests and verify

**Files:**
- Modify: `rust/xproc/tests/smoke.rs`

- [ ] Add varlen retry and observer tests.
- [ ] Run `cargo test -p xproc`.
- [ ] Re-run `./build/tests/xproc_capi_smoke_tests` to confirm the C side still passes.
