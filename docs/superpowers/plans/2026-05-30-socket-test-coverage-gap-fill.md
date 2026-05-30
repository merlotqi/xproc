# Socket Test Coverage Gap-Fill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 3 targeted C++ test cases to close P2 socket test coverage gaps and update the Phase 2 reference design checklist.

**Architecture:** Three independent test cases added to the existing `SocketTransport` suite in `tests/socket_transport_test.cpp`, plus a doc update to the reference design. No implementation changes.

**Tech Stack:** GoogleTest, C++17, POSIX/Win32 sockets

---

### Task 1: Add `FixedTcpLoopbackIPv4` test

**Files:**
- Modify: `tests/socket_transport_test.cpp` — insert after `FixedTcpLoopbackIPv6` test (after line 370)

- [ ] **Step 1: Add the test case**

The existing `run_fixed_loopback` helper accepts a host string. `FixedTcpLoopbackIPv6` calls `run_fixed_loopback("::1")`. Add a symmetric IPv4 test right after it:

```cpp
TEST(SocketTransport, FixedTcpLoopbackIPv4) { run_fixed_loopback("127.0.0.1"); }
```

- [ ] **Step 2: Build and run the new test**

```bash
cmake --build build --target socket_transport_test 2>&1 | tail -5
```

Expected: build succeeds.

```bash
./build/tests/socket_transport_test --gtest_filter='SocketTransport.FixedTcpLoopbackIPv4' 2>&1
```

Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 3: Verify no regressions**

```bash
./build/tests/socket_transport_test 2>&1 | tail -3
```

Expected: all 20 tests pass (19 existing + 1 new), `[  PASSED  ] 20 tests.`

- [ ] **Step 4: Commit**

```bash
git add tests/socket_transport_test.cpp
git commit -m "test: add FixedTcpLoopbackIPv4 for symmetric IPv4 fixed-frame coverage"
```

---

### Task 2: Add `FixedBytesZeroPaddedRoundtrip` test

**Files:**
- Modify: `tests/socket_transport_test.cpp` — insert after Gap 1 test (after the `FixedTcpLoopbackIPv4` line)

- [ ] **Step 1: Add the test case**

`send_fixed_bytes` pads short data with zeros up to `item_size`. This path is untested — all existing fixed tests use `send_fixed_sized` which expects exact length.

```cpp
TEST(SocketTransport, FixedBytesZeroPaddedRoundtrip) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::fixed;
    co.item_size = 8;
    co.socket_host.clear();

    xproc::ipc::socket_consumer cons(co);

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = cons.options().socket_port;
    po.type = xproc::ipc::channel_type::fixed;
    po.item_size = 8;

    xproc::ipc::socket_producer prod(po);

    const std::array<char, 4> payload{{'A', 'B', 'C', 'D'}};
    prod.send_fixed_bytes(payload.data(), payload.size());

    std::array<char, 8> received{};
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        ASSERT_EQ(len, 8u);
        std::memcpy(received.data(), p, 8);
      });
    }));

    EXPECT_EQ(received[0], 'A');
    EXPECT_EQ(received[1], 'B');
    EXPECT_EQ(received[2], 'C');
    EXPECT_EQ(received[3], 'D');
    EXPECT_EQ(received[4], '\0');
    EXPECT_EQ(received[5], '\0');
    EXPECT_EQ(received[6], '\0');
    EXPECT_EQ(received[7], '\0');
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}
```

- [ ] **Step 2: Build and run the new test**

```bash
cmake --build build --target socket_transport_test 2>&1 | tail -5
```

Expected: build succeeds.

```bash
./build/tests/socket_transport_test --gtest_filter='SocketTransport.FixedBytesZeroPaddedRoundtrip' 2>&1
```

Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 3: Verify no regressions**

```bash
./build/tests/socket_transport_test 2>&1 | tail -3
```

Expected: all 21 tests pass, `[  PASSED  ] 21 tests.`

- [ ] **Step 4: Commit**

```bash
git add tests/socket_transport_test.cpp
git commit -m "test: add FixedBytesZeroPaddedRoundtrip to cover send_fixed_bytes zero-pad path"
```

---

### Task 3: Add `SingleListenerServesBothIPv4AndIPv6` test

**Files:**
- Modify: `tests/socket_transport_test.cpp` — insert after Gap 2 test

- [ ] **Step 1: Add the test case**

A single listener should accept both IPv4 and IPv6 producers sequentially. This explicitly exercises the dual-stack binding path that existing tests only cover implicitly (each test creates its own consumer).

```cpp
TEST(SocketTransport, SingleListenerServesBothIPv4AndIPv6) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::varlen;
    co.socket_host.clear();

    xproc::ipc::socket_consumer cons(co);
    const std::uint16_t port = cons.options().socket_port;

    // IPv4 producer
    {
      xproc::ipc::transport_options po;
      po.backend = xproc::ipc::transport_backend::socket;
      po.socket_listen = false;
      po.socket_host = "127.0.0.1";
      po.socket_port = port;
      po.type = xproc::ipc::channel_type::varlen;

      xproc::ipc::socket_producer prod(po);
      const char* msg = "ipv4-msg";
      prod.send_varlen(msg, static_cast<std::uint32_t>(std::strlen(msg)));
    }

    std::string v4;
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        v4.assign(static_cast<const char*>(p), static_cast<std::size_t>(len));
      });
    }));
    EXPECT_EQ(v4, "ipv4-msg");

    // IPv6 producer — same consumer, same listen socket
    {
      xproc::ipc::transport_options po;
      po.backend = xproc::ipc::transport_backend::socket;
      po.socket_listen = false;
      po.socket_host = "::1";
      po.socket_port = port;
      po.type = xproc::ipc::channel_type::varlen;

      xproc::ipc::socket_producer prod(po);
      const char* msg = "ipv6-msg";
      prod.send_varlen(msg, static_cast<std::uint32_t>(std::strlen(msg)));
    }

    std::string v6;
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        v6.assign(static_cast<const char*>(p), static_cast<std::size_t>(len));
      });
    }));
    EXPECT_EQ(v6, "ipv6-msg");
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}
```

- [ ] **Step 2: Build and run the new test**

```bash
cmake --build build --target socket_transport_test 2>&1 | tail -5
```

Expected: build succeeds.

```bash
./build/tests/socket_transport_test --gtest_filter='SocketTransport.SingleListenerServesBothIPv4AndIPv6' 2>&1
```

Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 3: Verify no regressions — full suite**

```bash
./build/tests/socket_transport_test 2>&1 | tail -3
```

Expected: all 22 tests pass, `[  PASSED  ] 22 tests.`

- [ ] **Step 4: Commit**

```bash
git add tests/socket_transport_test.cpp
git commit -m "test: add SingleListenerServesBothIPv4AndIPv6 for explicit dual-stack coverage"
```

---

### Task 4: Update Phase 2 reference design checklist

**Files:**
- Modify: `.worktrees/ai-superpowers/docs/superpowers/reference/2026-05-28-phase2-reference-design.md` — P2 Socket test coverage section (lines 132–137)

- [ ] **Step 1: Mark completed checklist items**

Search for the section:

```
### P2: Socket test coverage
```

Current state (lines 132–137):

```markdown
### P2: Socket test coverage

- [ ] Fixed-frame socket roundtrip tests exist
- [ ] Disconnect/reconnect tests exist for both sides
- [ ] `ipc::runtime` over `socket_consumer` is tested
- [ ] Dual-stack edge cases are covered on Linux and macOS
```

Replace with:

```markdown
### P2: Socket test coverage

- [x] Fixed-frame socket roundtrip tests exist
- [x] Disconnect/reconnect tests exist for both sides
- [x] `ipc::runtime` over `socket_consumer` is tested
- [x] Dual-stack edge cases are covered on Linux and macOS
```

- [ ] **Step 2: Commit**

```bash
git add .worktrees/ai-superpowers/docs/superpowers/reference/2026-05-28-phase2-reference-design.md
git commit -m "docs: mark P2 socket test coverage checklist as complete"
```

---

### Task 5: Final verification

- [ ] **Step 1: Full C++ test suite**

```bash
cmake --build build --target xproc_run_tests 2>&1 | tail -20
```

Expected: all tests pass, no failures.

- [ ] **Step 2: Run CTest for completeness**

```bash
cd build && ctest --output-on-failure 2>&1 | tail -20
```

Expected: 100% tests passed.
