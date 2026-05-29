# Socket Builder API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a C++ socket builder API so socket consumers and producers can be opened without manually filling `transport_options`.

**Architecture:** Add a small header-only `socket_builders.hpp` beside `shm_builders.hpp`. The builders produce validated `transport_options`, return socket-specific endpoints from `open_consumer()` / `open_producer()`, and keep direct `.options()` access for factory/runtime integration. Existing socket transport behavior stays unchanged.

**Tech Stack:** C++17, header-only API builders, existing `socket_consumer` / `socket_producer`, GoogleTest, CMake/Ninja.

---

## File Structure

- Create `include/xproc/ipc/socket_builders.hpp`: fluent C++ socket listener/connector builders and public entry functions.
- Modify `include/xproc/xproc.hpp`: export the new builder header through the umbrella include.
- Modify `tests/api_surface_test.cpp`: add builder option, validation, and loopback tests.
- Modify `examples/socket_varlen_reconnect_demo.cpp`: replace local manual `transport_options` helpers with builder calls.
- No changes to `transport_options`, socket wire protocol, C API, or binding APIs.

## Task 1: Add Failing Socket Builder API Surface Tests

**Files:**
- Modify: `tests/api_surface_test.cpp`

- [ ] **Step 1: Add socket builder option and validation tests**

Append these tests near the existing builder API tests in `tests/api_surface_test.cpp`:

```cpp
TEST(ApiSurface, SocketBuildersProduceExpectedOptions) {
  const auto varlen_listener = xproc::ipc::listen_varlen_socket();
  const auto varlen_listener_opts = varlen_listener.options();
  EXPECT_EQ(varlen_listener_opts.backend, xproc::ipc::transport_backend::socket);
  EXPECT_EQ(varlen_listener_opts.type, xproc::ipc::channel_type::varlen);
  EXPECT_TRUE(varlen_listener_opts.socket_listen);
  EXPECT_TRUE(varlen_listener_opts.socket_host.empty());
  EXPECT_EQ(varlen_listener_opts.socket_port, 0u);
  EXPECT_EQ(varlen_listener_opts.item_size, 0u);

  const auto fixed_listener_opts = xproc::ipc::listen_fixed_socket(sizeof(std::uint32_t))
                                       .with_port(12345)
                                       .options();
  EXPECT_EQ(fixed_listener_opts.backend, xproc::ipc::transport_backend::socket);
  EXPECT_EQ(fixed_listener_opts.type, xproc::ipc::channel_type::fixed);
  EXPECT_TRUE(fixed_listener_opts.socket_listen);
  EXPECT_TRUE(fixed_listener_opts.socket_host.empty());
  EXPECT_EQ(fixed_listener_opts.socket_port, 12345u);
  EXPECT_EQ(fixed_listener_opts.item_size, sizeof(std::uint32_t));

  const auto varlen_connector_opts = xproc::ipc::connect_varlen_socket("127.0.0.1", 54321)
                                         .with_connect_retries(7)
                                         .with_connect_retry_ms(3)
                                         .options();
  EXPECT_EQ(varlen_connector_opts.backend, xproc::ipc::transport_backend::socket);
  EXPECT_EQ(varlen_connector_opts.type, xproc::ipc::channel_type::varlen);
  EXPECT_FALSE(varlen_connector_opts.socket_listen);
  EXPECT_EQ(varlen_connector_opts.socket_host, "127.0.0.1");
  EXPECT_EQ(varlen_connector_opts.socket_port, 54321u);
  EXPECT_EQ(varlen_connector_opts.item_size, 0u);
  EXPECT_EQ(varlen_connector_opts.socket_connect_retries, 7);
  EXPECT_EQ(varlen_connector_opts.socket_connect_retry_ms, 3);

  const auto fixed_connector_opts = xproc::ipc::connect_fixed_socket("::1", 4444, sizeof(std::uint64_t)).options();
  EXPECT_EQ(fixed_connector_opts.backend, xproc::ipc::transport_backend::socket);
  EXPECT_EQ(fixed_connector_opts.type, xproc::ipc::channel_type::fixed);
  EXPECT_FALSE(fixed_connector_opts.socket_listen);
  EXPECT_EQ(fixed_connector_opts.socket_host, "::1");
  EXPECT_EQ(fixed_connector_opts.socket_port, 4444u);
  EXPECT_EQ(fixed_connector_opts.item_size, sizeof(std::uint64_t));
}

TEST(ApiSurface, SocketBuildersRejectInvalidOptions) {
  EXPECT_THROW((void)xproc::ipc::listen_fixed_socket(0).options(), std::invalid_argument);
  EXPECT_THROW((void)xproc::ipc::connect_fixed_socket("127.0.0.1", 1234, 0).options(), std::invalid_argument);
  EXPECT_THROW((void)xproc::ipc::connect_varlen_socket("", 1234).options(), std::invalid_argument);
  EXPECT_THROW((void)xproc::ipc::connect_varlen_socket("127.0.0.1", 0).options(), std::invalid_argument);
  EXPECT_THROW((void)xproc::ipc::connect_varlen_socket("127.0.0.1", 1234).with_connect_retries(-1).options(),
               std::invalid_argument);
  EXPECT_THROW((void)xproc::ipc::connect_varlen_socket("127.0.0.1", 1234).with_connect_retry_ms(-1).options(),
               std::invalid_argument);
}
```

- [ ] **Step 2: Add a real socket roundtrip test through builders**

Append this test after `SocketBuildersRejectInvalidOptions`:

```cpp
TEST(ApiSurface, SocketBuildersOpenSpecificEndpointsAndRoundTrip) {
  try {
    auto consumer = xproc::ipc::listen_varlen_socket().open_consumer();
    auto producer = xproc::ipc::connect_varlen_socket("127.0.0.1", consumer.options().socket_port)
                        .with_connect_retries(20)
                        .with_connect_retry_ms(1)
                        .open_producer();

    static_assert(std::is_same_v<decltype(consumer), xproc::ipc::socket_consumer>);
    static_assert(std::is_same_v<decltype(producer), xproc::ipc::socket_producer>);

    const std::string expected = "socket-builder-roundtrip";
    producer.send_varlen(expected.data(), static_cast<std::uint32_t>(expected.size()));

    std::string actual;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (actual.empty() && std::chrono::steady_clock::now() < deadline) {
      const bool got = consumer.poll([&](void* p, std::uint32_t len) {
        actual.assign(static_cast<const char*>(p), static_cast<std::size_t>(len));
      });
      if (!got) {
        consumer.wait();
      }
    }

    EXPECT_EQ(actual, expected);
  } catch (const std::runtime_error& ex) {
    GTEST_SKIP() << "socket transport unavailable in this environment: " << ex.what();
  }
}
```

- [ ] **Step 3: Ensure required includes are present**

`<string>` is already included. If `<type_traits>` is missing, add it:

```cpp
#include <type_traits>
```

- [ ] **Step 4: Run the focused tests and verify they fail**

Run:

```bash
cmake --build build --target xproc_api_surface_test
ctest --test-dir build -R "ApiSurface.SocketBuilders" --output-on-failure
```

Expected: compile failure mentioning missing `xproc::ipc::listen_varlen_socket`, `listen_fixed_socket`, `connect_varlen_socket`, or `connect_fixed_socket`.

## Task 2: Implement Header-Only Socket Builders

**Files:**
- Create: `include/xproc/ipc/socket_builders.hpp`
- Modify: `include/xproc/xproc.hpp`
- Test: `tests/api_surface_test.cpp`

- [ ] **Step 1: Create `socket_builders.hpp`**

Create `include/xproc/ipc/socket_builders.hpp` with this content:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/socket_channel.hpp>

namespace xproc::ipc {

class socket_listener_builder {
 public:
  socket_listener_builder(channel_type type, std::uint32_t item_size) : type_(type), item_size_(item_size) {}

  socket_listener_builder& with_port(std::uint16_t port) {
    port_ = port;
    return *this;
  }

  transport_options options() const {
    transport_options opts;
    opts.backend = transport_backend::socket;
    opts.type = type_;
    opts.item_size = item_size_;
    opts.socket_listen = true;
    opts.socket_host.clear();
    opts.socket_port = port_;
    validate_consumer_transport_options(opts);
    return opts;
  }

  socket_consumer open_consumer() const { return socket_consumer(options()); }

 private:
  channel_type type_;
  std::uint32_t item_size_{0};
  std::uint16_t port_{0};
};

class socket_connector_builder {
 public:
  socket_connector_builder(channel_type type, std::uint32_t item_size, std::string host, std::uint16_t port)
      : type_(type), item_size_(item_size), host_(std::move(host)), port_(port) {}

  socket_connector_builder& with_connect_retries(int retries) {
    connect_retries_ = retries;
    return *this;
  }

  socket_connector_builder& with_connect_retry_ms(int retry_ms) {
    connect_retry_ms_ = retry_ms;
    return *this;
  }

  transport_options options() const {
    transport_options opts;
    opts.backend = transport_backend::socket;
    opts.type = type_;
    opts.item_size = item_size_;
    opts.socket_listen = false;
    opts.socket_host = host_;
    opts.socket_port = port_;
    opts.socket_connect_retries = connect_retries_;
    opts.socket_connect_retry_ms = connect_retry_ms_;
    validate_producer_transport_options(opts);
    return opts;
  }

  socket_producer open_producer() const { return socket_producer(options()); }

 private:
  channel_type type_;
  std::uint32_t item_size_{0};
  std::string host_;
  std::uint16_t port_{0};
  int connect_retries_{transport_options{}.socket_connect_retries};
  int connect_retry_ms_{transport_options{}.socket_connect_retry_ms};
};

using socket_varlen_listener_builder = socket_listener_builder;
using socket_fixed_listener_builder = socket_listener_builder;
using socket_varlen_connector_builder = socket_connector_builder;
using socket_fixed_connector_builder = socket_connector_builder;

inline socket_varlen_listener_builder listen_varlen_socket() {
  return socket_listener_builder(channel_type::varlen, 0u);
}

inline socket_fixed_listener_builder listen_fixed_socket(std::uint32_t item_size) {
  return socket_listener_builder(channel_type::fixed, item_size);
}

inline socket_varlen_connector_builder connect_varlen_socket(std::string host, std::uint16_t port) {
  return socket_connector_builder(channel_type::varlen, 0u, std::move(host), port);
}

inline socket_fixed_connector_builder connect_fixed_socket(std::string host, std::uint16_t port,
                                                           std::uint32_t item_size) {
  return socket_connector_builder(channel_type::fixed, item_size, std::move(host), port);
}

}  // namespace xproc::ipc
```

- [ ] **Step 2: Export the new header from the umbrella include**

In `include/xproc/xproc.hpp`, add this include immediately after `socket_channel.hpp` or near the other IPC includes:

```cpp
#include <xproc/ipc/socket_builders.hpp>
```

- [ ] **Step 3: Run the focused API surface tests**

Run:

```bash
cmake --build build --target xproc_api_surface_test
ctest --test-dir build -R "ApiSurface.SocketBuilders" --output-on-failure
```

Expected: all `ApiSurface.SocketBuilders...` tests pass.

- [ ] **Step 4: Run the related socket tests**

Run:

```bash
cmake --build build --target xproc_socket_transport_test
ctest --test-dir build -R "SocketTransport" --output-on-failure
```

Expected: existing socket transport tests pass unchanged.

- [ ] **Step 5: Commit**

```bash
git add include/xproc/ipc/socket_builders.hpp include/xproc/xproc.hpp tests/api_surface_test.cpp
git commit -m "feat: add socket transport builders"
```

## Task 3: Migrate the Socket Reconnect Example to Builders

**Files:**
- Modify: `examples/socket_varlen_reconnect_demo.cpp`
- Test: `examples/socket_varlen_reconnect_demo.cpp`

- [ ] **Step 1: Replace manual option helper functions, keep everything else**

Delete only the two `make_socket_*_options` helper functions (and their implementations) from the anonymous namespace:

```cpp
xproc::ipc::transport_options make_socket_consumer_options();
xproc::ipc::transport_options make_socket_producer_options(std::uint16_t port);
```

Keep `poll_until`, `wait_until_consumer_drops_stale_peer`, and all other helpers as-is.

Replace the endpoint setup in `main()`:

```cpp
    auto consumer = xproc::ipc::listen_varlen_socket().open_consumer();
    auto producer = xproc::ipc::connect_varlen_socket("127.0.0.1", consumer.options().socket_port)
                        .with_connect_retries(50)
                        .with_connect_retry_ms(2)
                        .open_producer();
```

The rest of `main()` is unchanged — keep the full reconnect verification flow including `is_connected()` checks, `wait_until_consumer_drops_stale_peer`, and both `poll_until` calls.

- [ ] **Step 2: Remove now-unused includes**

If they are no longer used after deleting the helper functions, remove unused includes from `examples/socket_varlen_reconnect_demo.cpp`.

The expected include block should be:

```cpp
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>
```

- [ ] **Step 3: Build the example**

Run:

```bash
cmake --build build --target xproc_socket_varlen_reconnect_demo
```

Expected: build succeeds.

- [ ] **Step 4: Run the example**

Run:

```bash
./build/examples/xproc_socket_varlen_reconnect_demo
```

Expected output includes:

```text
consumer received: before reconnect
producer reconnecting; old peer should be discarded by consumer
consumer dropped stale peer and is ready for the reconnected producer
consumer received: after reconnect
```

- [ ] **Step 5: Commit**

```bash
git add examples/socket_varlen_reconnect_demo.cpp
git commit -m "examples: use socket builder API"
```

## Task 4: Final Verification

**Files:**
- No source changes expected.

- [ ] **Step 1: Run the complete focused build**

Run:

```bash
cmake --build build --target xproc_api_surface_test xproc_socket_transport_test xproc_socket_varlen_reconnect_demo
```

Expected: build succeeds.

- [ ] **Step 2: Run focused tests**

Run:

```bash
ctest --test-dir build -R "ApiSurface|SocketTransport" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 3: Run the example**

Run:

```bash
./build/examples/xproc_socket_varlen_reconnect_demo
```

Expected output includes:

```text
consumer received: before reconnect
producer reconnecting; old peer should be discarded by consumer
consumer dropped stale peer and is ready for the reconnected producer
consumer received: after reconnect
```

- [ ] **Step 4: Check whitespace and repository status**

Run:

```bash
git diff --check
git status --short
git log --oneline -n 8
```

Expected:

- `git diff --check` prints no errors
- `git status --short` shows no uncommitted source changes
- the latest commits are `examples: use socket builder API` and `feat: add socket transport builders`

