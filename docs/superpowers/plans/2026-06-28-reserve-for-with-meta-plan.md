# reserve_for + message_meta Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `message_meta` parameter to `reserve_for` in ring buffer writers with three-phase backoff, then simplify channel layer to delegate directly.

**Architecture:** Three-file refactor. Writers gain a `reserve_for(size, timeout, meta)` overload with spin→yield→sleep backoff matching `atomic_backoff` constants. Existing `reserve_for(size, timeout)` becomes a thin wrapper. Channel `send_*_for` methods drop their inlined loops and call `reserve_for` directly.

**Tech Stack:** C++17, no new dependencies

## Global Constraints

- C++17 standard, header-only library
- Backoff constants must match `atomic_backoff`: 12 spin + 10 yield
- Public API of `channel`, `producer`, `consumer` unchanged
- All existing tests must pass
- Cross-platform: Linux (futex), macOS (Darwin primitives), Windows (Win32)

---

### Task 1: Add `reserve_for` with meta to `fixed_writer.hpp`

**Files:**
- Modify: `include/xproc/ringbuffer/fixed_writer.hpp:69-82`

**Interfaces:**
- Consumes: `try_reserve(uint32_t, const message_meta&)` — already exists
- Consumes: `reserve_status`, `reserve_result` — already exist
- Produces: `reserve_result reserve_for(uint32_t, const duration&, const message_meta&)` — new overload
- Produces: `reserve_result reserve_for(uint32_t, const duration&)` — refactored to delegate

- [ ] **Step 1: Build and run existing tests to establish baseline**

Run: `cd build && cmake --build . --target xproc_tests -j$(nproc 2>/dev/null || sysctl -n hw.ncpu) 2>&1 | tail -5`

- [ ] **Step 2: Run tests to verify baseline is green**

Run: `cd build && ctest --output-on-failure 2>&1 | tail -20`
Expected: All tests pass

- [ ] **Step 3: Add the new `reserve_for` overload with three-phase backoff**

Replace lines 69-82 in `include/xproc/ringbuffer/fixed_writer.hpp`:

```cpp
  template <typename Rep, typename Period>
  reserve_result reserve_for(uint32_t item_size, const std::chrono::duration<Rep, Period>& timeout) {
    return reserve_for(item_size, timeout, xproc::ipc::message_meta{});
  }

  template <typename Rep, typename Period>
  reserve_result reserve_for(uint32_t item_size, const std::chrono::duration<Rep, Period>& timeout,
                             const xproc::ipc::message_meta& meta) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::uint32_t iteration = 0;
    while (true) {
      reserve_result rr = try_reserve(item_size, meta);
      if (rr.status != reserve_status::full) {
        return rr;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return {reserve_status::timeout, nullptr, 0};
      }
      ++iteration;
      if (iteration <= 12) {
        const std::uint32_t exp = (std::min)(iteration - 1, 8u);
        const std::uint32_t delay = 1u << exp;
        for (std::uint32_t i = 0; i < delay; ++i) {
          XPROC_CPU_PAUSE();
        }
      } else if (iteration <= 22) {
        std::this_thread::yield();
      } else {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
          return {reserve_status::timeout, nullptr, 0};
        }
        std::this_thread::sleep_for((std::min)(remaining, std::chrono::milliseconds(1)));
      }
    }
  }
```

- [ ] **Step 4: Build to verify compilation**

Run: `cd build && cmake --build . --target xproc_tests -j$(nproc 2>/dev/null || sysctl -n hw.ncpu) 2>&1 | tail -10`
Expected: Compilation succeeds with no errors

- [ ] **Step 5: Run tests to verify no regressions**

Run: `cd build && ctest --output-on-failure 2>&1 | tail -20`
Expected: All tests pass

- [ ] **Step 6: Commit**

```bash
git add include/xproc/ringbuffer/fixed_writer.hpp
git commit -m "feat: add reserve_for with message_meta support to fixed_writer

Replace the bare yield loop in reserve_for with a three-phase backoff
(spin → yield → sleep) matching atomic_backoff constants. The existing
reserve_for(size, timeout) overload delegates to the new meta-aware
version with an empty message_meta{}.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Add `reserve_for` with meta to `varlen_writer.hpp`

**Files:**
- Modify: `include/xproc/ringbuffer/varlen_writer.hpp:78-91`

**Interfaces:**
- Consumes: `try_reserve(uint32_t, const message_meta&)` — already exists
- Produces: `reserve_result reserve_for(uint32_t, const duration&, const message_meta&)` — new overload
- Produces: `reserve_result reserve_for(uint32_t, const duration&)` — refactored to delegate

- [ ] **Step 1: Replace `reserve_for` with the new overloads**

Replace lines 78-91 in `include/xproc/ringbuffer/varlen_writer.hpp`:

```cpp
  template <typename Rep, typename Period>
  reserve_result reserve_for(uint32_t len, const std::chrono::duration<Rep, Period>& timeout) {
    return reserve_for(len, timeout, xproc::ipc::message_meta{});
  }

  template <typename Rep, typename Period>
  reserve_result reserve_for(uint32_t len, const std::chrono::duration<Rep, Period>& timeout,
                             const xproc::ipc::message_meta& meta) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::uint32_t iteration = 0;
    while (true) {
      reserve_result rr = try_reserve(len, meta);
      if (rr.status != reserve_status::full) {
        return rr;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return {reserve_status::timeout, nullptr, 0};
      }
      ++iteration;
      if (iteration <= 12) {
        const std::uint32_t exp = (std::min)(iteration - 1, 8u);
        const std::uint32_t delay = 1u << exp;
        for (std::uint32_t i = 0; i < delay; ++i) {
          XPROC_CPU_PAUSE();
        }
      } else if (iteration <= 22) {
        std::this_thread::yield();
      } else {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
          return {reserve_status::timeout, nullptr, 0};
        }
        std::this_thread::sleep_for((std::min)(remaining, std::chrono::milliseconds(1)));
      }
    }
  }
```

- [ ] **Step 2: Build to verify compilation**

Run: `cd build && cmake --build . --target xproc_tests -j$(nproc 2>/dev/null || sysctl -n hw.ncpu) 2>&1 | tail -10`
Expected: Compilation succeeds with no errors

- [ ] **Step 3: Run tests to verify no regressions**

Run: `cd build && ctest --output-on-failure 2>&1 | tail -20`
Expected: All tests pass

- [ ] **Step 4: Commit**

```bash
git add include/xproc/ringbuffer/varlen_writer.hpp
git commit -m "feat: add reserve_for with message_meta support to varlen_writer

Mirrors the fixed_writer change: three-phase backoff (spin → yield → sleep)
and a meta-aware overload. Existing reserve_for delegates to the new version.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Simplify channel.hpp `send_*_for` methods

**Files:**
- Modify: `include/xproc/ipc/channel.hpp:175-215` (`send_fixed_sized_for`)
- Modify: `include/xproc/ipc/channel.hpp:271-303` (`send_varlen_for`)

**Interfaces:**
- Consumes: `fixed_writer::reserve_for(uint32_t, const duration&, const message_meta&)` — from Task 1
- Consumes: `varlen_writer::reserve_for(uint32_t, const duration&, const message_meta&)` — from Task 2
- Public API of `channel` / `producer` / `consumer` — unchanged

- [ ] **Step 1: Simplify `send_fixed_sized_for`**

Replace lines 175-215 in `include/xproc/ipc/channel.hpp` (the two `send_fixed_sized_for` overloads — one without meta, one with meta):

The overload without meta simplifies from an inlined loop to direct delegation:
```cpp
  template <typename Rep, typename Period>
  send_result send_fixed_sized_for(const void* data, std::uint32_t byte_length,
                                   const std::chrono::duration<Rep, Period>& timeout) {
    return send_fixed_sized_for(data, byte_length, timeout, message_meta{});
  }
```

The overload with meta replaces the inlined `while(true) { try_reserve ... yield }` loop with a `reserve_for` call:
```cpp
  template <typename Rep, typename Period>
  send_result send_fixed_sized_for(const void* data, std::uint32_t byte_length,
                                   const std::chrono::duration<Rep, Period>& timeout, const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_fixed_sized_for requires producer role");
    }
    if (opts_.type != channel_type::fixed) {
      throw std::logic_error("channel::send_fixed_sized_for requires fixed channel");
    }
    if (byte_length > opts_.item_size) {
      return send_result::invalid_argument;
    }
    auto* fw = static_cast<ringbuffer::fixed_writer*>(writer_.get());
    auto rr = fw->reserve_for(opts_.item_size, timeout, meta);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, static_cast<std::size_t>(byte_length));
    if (byte_length < opts_.item_size) {
      std::memset(static_cast<char*>(rr.payload) + byte_length, 0,
                  static_cast<std::size_t>(opts_.item_size - byte_length));
    }
    fw->commit(rr.position);
    return send_result::ok;
  }
```

- [ ] **Step 2: Simplify `send_varlen_for`**

Replace lines 271-303 in `include/xproc/ipc/channel.hpp` (the two `send_varlen_for` overloads):

The overload without meta:
```cpp
  template <typename Rep, typename Period>
  send_result send_varlen_for(const void* data, std::uint32_t len, const std::chrono::duration<Rep, Period>& timeout) {
    return send_varlen_for(data, len, timeout, message_meta{});
  }
```

The overload with meta:
```cpp
  template <typename Rep, typename Period>
  send_result send_varlen_for(const void* data, std::uint32_t len, const std::chrono::duration<Rep, Period>& timeout,
                              const message_meta& meta) {
    if (get_role() != role::producer) {
      throw std::logic_error("channel::send_varlen_for requires producer role");
    }
    if (opts_.type != channel_type::varlen) {
      throw std::logic_error("channel::send_varlen_for requires variable channel");
    }
    auto* vw = static_cast<ringbuffer::varlen_writer*>(writer_.get());
    auto rr = vw->reserve_for(len, timeout, meta);
    if (!rr) {
      return map_reserve_status(rr.status);
    }
    std::memcpy(rr.payload, data, len);
    vw->commit(rr.position);
    return send_result::ok;
  }
```

- [ ] **Step 3: Build to verify compilation**

Run: `cd build && cmake --build . --target xproc_tests -j$(nproc 2>/dev/null || sysctl -n hw.ncpu) 2>&1 | tail -10`
Expected: Compilation succeeds with no errors

- [ ] **Step 4: Run tests to verify no regressions**

Run: `cd build && ctest --output-on-failure 2>&1 | tail -20`
Expected: All tests pass

- [ ] **Step 5: Commit**

```bash
git add include/xproc/ipc/channel.hpp
git commit -m "refactor: simplify channel send_*_for by delegating to reserve_for with meta

Replace inlined try_reserve + yield loops in send_fixed_sized_for and
send_varlen_for with direct calls to fixed_writer::reserve_for and
varlen_writer::reserve_for. Removes the TODO comments about restoring
efficient OS-level waiting — the three-phase backoff is now in reserve_for.

Co-Authored-By: Claude <noreply@anthropic.com>"
```
