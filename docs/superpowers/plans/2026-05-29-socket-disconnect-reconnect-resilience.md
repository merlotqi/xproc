# Socket Disconnect / Reconnect Resilience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden socket disconnect/reconnect behavior with explicit producer recovery, transparent consumer peer replacement, and interruptible socket-backed runtime shutdown.

**Architecture:** Add a small optional wait-interruption hook to the generic consumer interface, then implement socket-specific wakeup and reconnect behavior inside `socket_channel`. Keep the socket wire protocol unchanged and keep reconnect APIs C++-only for this phase; C API coverage verifies existing exception-to-status mapping.

**Tech Stack:** C++17, CMake/Ninja, GoogleTest, POSIX sockets, WinSock, existing xproc C API.

---

## File Structure

- Modify `include/xproc/ipc/channel_interface.hpp`: add an optional `interrupt_wait()` hook with a no-op default and update the wait contract comment.
- Modify `include/xproc/ipc/runtime.hpp`: call `interrupt_wait()` from `runtime::stop()` for interface-backed consumers.
- Modify `include/xproc/ipc/socket_channel.hpp`: expose `socket_producer::{is_connected,reconnect,try_reconnect}`, expose `socket_consumer::is_connected`, override `socket_consumer::interrupt_wait`, and add an opaque wakeup helper pointer.
- Modify `src/ipc/socket_channel.cpp`: add reusable producer connect retry logic, close stale producer sockets after write failure, add select-compatible wakeup sockets, make `socket_consumer::wait()` block on peer/listen readiness plus wakeup, and keep consumer partial-frame cleanup.
- Modify `tests/runtime_allocation_test.cpp`: add a deterministic interface runtime stop test using a blocking fake consumer.
- Modify `tests/socket_transport_test.cpp`: add producer reconnect tests, socket wait interruption tests, and partial-frame recovery tests.
- Modify `tests/capi_smoke_test.cpp`: add a C API send-failure mapping test.

## Task 1: Add Interface Wait Interruption

**Files:**
- Modify: `include/xproc/ipc/channel_interface.hpp`
- Modify: `include/xproc/ipc/runtime.hpp`
- Test: `tests/runtime_allocation_test.cpp`

- [ ] **Step 1: Write the failing runtime interface stop test**

Add these includes to `tests/runtime_allocation_test.cpp`:

```cpp
#include <condition_variable>
#include <mutex>
```

Add this fake consumer inside the anonymous namespace after `Fixture`:

```cpp
class BlockingInterfaceConsumer final : public xproc::ipc::consumer_channel_interface {
 public:
  const xproc::ipc::transport_options& options() const noexcept override { return opts_; }

  void wait() override {
    std::unique_lock<std::mutex> lock(mu_);
    entered_wait_.store(true, std::memory_order_release);
    cv_.notify_all();
    cv_.wait(lock, [&] { return interrupted_; });
  }

  void interrupt_wait() noexcept override {
    std::lock_guard<std::mutex> lock(mu_);
    interrupted_ = true;
    cv_.notify_all();
  }

  bool wait_entered_for(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (entered_wait_.load(std::memory_order_acquire)) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return entered_wait_.load(std::memory_order_acquire);
  }

 protected:
  bool poll_impl(const std::function<void(void*, std::uint32_t)>&) override { return false; }

 private:
  xproc::ipc::transport_options opts_{};
  std::mutex mu_;
  std::condition_variable cv_;
  bool interrupted_{false};
  std::atomic<bool> entered_wait_{false};
};
```

Add this test near the existing consumer interface path tests:

```cpp
TEST(RuntimeAllocation, StopInterruptsInterfaceWait) {
  BlockingInterfaceConsumer cons;
  xproc::ipc::runtime rt(cons);
  std::atomic<bool> returned{false};

  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor, [](const std::uint8_t*, std::size_t) {});
    returned.store(true, std::memory_order_release);
  });

  ASSERT_TRUE(cons.wait_entered_for(std::chrono::milliseconds(250)));
  rt.stop();
  rt_thread.join();
  EXPECT_TRUE(returned.load(std::memory_order_acquire));
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```bash
cmake --build build --target xproc_runtime_allocation_test
ctest --test-dir build -R RuntimeAllocation.StopInterruptsInterfaceWait --output-on-failure
```

Expected: compile failure because `consumer_channel_interface` does not yet declare `interrupt_wait()`.

- [ ] **Step 3: Add the optional interface hook**

In `include/xproc/ipc/channel_interface.hpp`, replace the wait comment and method block with:

```cpp
  /// Called when poll returned false: block until new data, an interrupt, or a backend-specific wake condition.
  virtual void wait() = 0;

  /// Interrupts a thread currently blocked in wait(). Backends that do not block in wait() may keep the default.
  virtual void interrupt_wait() noexcept {}
```

- [ ] **Step 4: Wake interface-backed runtime loops from stop**

In `include/xproc/ipc/runtime.hpp`, change `runtime::stop()` to:

```cpp
  void stop() {
    running_.store(false, std::memory_order_release);
    if (shm_ != nullptr && shm_->header() != nullptr) {
      shm_->header()->rb_meta.commit_seq.fetch_add(1, std::memory_order_release);
      sync::atomic_notify_all(&shm_->header()->rb_meta.commit_seq);
    }
    if (iface_ != nullptr) {
      iface_->interrupt_wait();
    }
  }
```

- [ ] **Step 5: Run the focused runtime test**

Run:

```bash
cmake --build build --target xproc_runtime_allocation_test
ctest --test-dir build -R RuntimeAllocation.StopInterruptsInterfaceWait --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/xproc/ipc/channel_interface.hpp include/xproc/ipc/runtime.hpp tests/runtime_allocation_test.cpp
git commit -m "feat: interrupt interface-backed runtime waits"
```

## Task 2: Make Socket Consumer Wait Interruptible

**Files:**
- Modify: `include/xproc/ipc/socket_channel.hpp`
- Modify: `src/ipc/socket_channel.cpp`
- Test: `tests/socket_transport_test.cpp`

- [ ] **Step 1: Write the failing socket wait interruption test**

Add this test to `tests/socket_transport_test.cpp` after the existing runtime socket test:

```cpp
TEST(SocketTransport, WaitBlocksUntilSocketConsumerIsInterrupted) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::varlen;
    co.socket_host.clear();
    xproc::ipc::socket_consumer cons(co);

    std::atomic<bool> returned{false};
    std::thread waiter([&] {
      cons.wait();
      returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    EXPECT_FALSE(returned.load(std::memory_order_acquire));

    cons.interrupt_wait();
    waiter.join();
    EXPECT_TRUE(returned.load(std::memory_order_acquire));
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```bash
cmake --build build --target xproc_socket_transport_test
ctest --test-dir build -R SocketTransport.WaitBlocksUntilSocketConsumerIsInterrupted --output-on-failure
```

Expected: FAIL because current `socket_consumer::wait()` returns after its 5 ms timeout instead of waiting for an explicit interrupt or socket readiness.

- [ ] **Step 3: Update the socket consumer header**

In `include/xproc/ipc/socket_channel.hpp`, add `<memory>` and forward-declare the wake helper:

```cpp
#include <memory>

namespace xproc::ipc {

class socket_wait_interruptor;
```

Update the public `socket_consumer` methods:

```cpp
  bool is_connected() const noexcept;
  void wait() override;
  void interrupt_wait() noexcept override;
```

Add the wake helper field before the platform socket fields:

```cpp
  std::unique_ptr<socket_wait_interruptor> wake_;
```

- [ ] **Step 4: Add the namespace-level socket wake helper in the `.cpp`**

In `src/ipc/socket_channel.cpp`, add these includes:

```cpp
#include <algorithm>
#include <array>
#include <memory>
```

Inside `namespace xproc::ipc`, before the anonymous namespace, define the helper class that matches the forward declaration from the header:

```cpp
class socket_wait_interruptor {
 public:
  socket_wait_interruptor();
  ~socket_wait_interruptor();

  socket_wait_interruptor(const socket_wait_interruptor&) = delete;
  socket_wait_interruptor& operator=(const socket_wait_interruptor&) = delete;

#if defined(_WIN32)
  SOCKET read_handle() const noexcept { return read_; }
#else
  int read_handle() const noexcept { return read_; }
#endif

  void notify() noexcept;
  void drain() noexcept;

 private:
#if defined(_WIN32)
  SOCKET read_{INVALID_SOCKET};
  SOCKET write_{INVALID_SOCKET};
#else
  int read_{-1};
  int write_{-1};
#endif
};
```

- [ ] **Step 5: Implement nonblocking and no-signal helpers**

Add these helper functions in the anonymous namespace near `close_handle()`:

```cpp
void set_nonblocking(sock_handle s) noexcept {
  if (is_invalid(s)) {
    return;
  }
#if defined(_WIN32)
  u_long mode = 1;
  (void)::ioctlsocket(s, FIONBIO, &mode);
#else
  const int flags = ::fcntl(s, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(s, F_SETFL, flags | O_NONBLOCK);
  }
#endif
}

int send_no_signal_flags() noexcept {
#if !defined(_WIN32) && defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}
```

- [ ] **Step 6: Implement `socket_wait_interruptor` with direct platform handles**

For POSIX, implement the constructor with `socketpair`:

```cpp
#if !defined(_WIN32)
socket_wait_interruptor::socket_wait_interruptor() {
  int fds[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    throw std::runtime_error("socket wait interruptor creation failed");
  }
  read_ = fds[0];
  write_ = fds[1];
  set_nonblocking(static_cast<sock_handle>(read_));
  set_nonblocking(static_cast<sock_handle>(write_));
}
#endif
```

For Windows, implement it with a loopback TCP pair:

```cpp
#if defined(_WIN32)
socket_wait_interruptor::socket_wait_interruptor() {
  winsock_init();
  SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) {
    throw std::runtime_error("socket wait interruptor listener creation failed");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listener, 1) != 0) {
    close_handle(listener);
    throw std::runtime_error("socket wait interruptor listener bind failed");
  }

  sockaddr_in bound{};
  int bound_len = sizeof(bound);
  if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
    close_handle(listener);
    throw std::runtime_error("socket wait interruptor getsockname failed");
  }

  SOCKET writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (writer == INVALID_SOCKET ||
      ::connect(writer, reinterpret_cast<sockaddr*>(&bound), sizeof(bound)) != 0) {
    close_handle(writer);
    close_handle(listener);
    throw std::runtime_error("socket wait interruptor connect failed");
  }

  SOCKET reader = ::accept(listener, nullptr, nullptr);
  close_handle(listener);
  if (reader == INVALID_SOCKET) {
    close_handle(writer);
    throw std::runtime_error("socket wait interruptor accept failed");
  }

  read_ = reader;
  write_ = writer;
  set_nonblocking(static_cast<sock_handle>(read_));
  set_nonblocking(static_cast<sock_handle>(write_));
}
#endif
```

Add the common destructor and methods:

```cpp
socket_wait_interruptor::~socket_wait_interruptor() {
#if defined(_WIN32)
  if (read_ != INVALID_SOCKET) {
    ::closesocket(read_);
  }
  if (write_ != INVALID_SOCKET) {
    ::closesocket(write_);
  }
#else
  if (read_ >= 0) {
    ::close(read_);
  }
  if (write_ >= 0) {
    ::close(write_);
  }
#endif
}

void socket_wait_interruptor::notify() noexcept {
  const char byte = 1;
#if defined(_WIN32)
  if (write_ == INVALID_SOCKET) {
    return;
  }
  (void)::send(write_, &byte, 1, 0);
#else
  if (write_ < 0) {
    return;
  }
  (void)::send(write_, &byte, 1, send_no_signal_flags());
#endif
}

void socket_wait_interruptor::drain() noexcept {
  std::array<char, 64> buf{};
  for (;;) {
#if defined(_WIN32)
    if (read_ == INVALID_SOCKET) {
      return;
    }
    const int n = ::recv(read_, buf.data(), static_cast<int>(buf.size()), 0);
#else
    if (read_ < 0) {
      return;
    }
    const ssize_t n = ::recv(read_, buf.data(), buf.size(), 0);
#endif
    if (n <= 0) {
      return;
    }
  }
}
```

- [ ] **Step 7: Initialize and use the wake helper**

Change the socket consumer constructor initializer in `src/ipc/socket_channel.cpp`:

```cpp
socket_consumer::socket_consumer(const transport_options& opts) : opts_(opts), wake_(std::make_unique<socket_wait_interruptor>()) {
```

Add the public state and interrupt methods:

```cpp
bool socket_consumer::is_connected() const noexcept {
#if defined(_WIN32)
  return sock_ != static_cast<std::uintptr_t>(INVALID_SOCKET);
#else
  return sock_ >= 0;
#endif
}

void socket_consumer::interrupt_wait() noexcept {
  if (wake_) {
    wake_->notify();
  }
}
```

Replace `socket_consumer::wait()` with a readiness wait that uses no timeout:

```cpp
void socket_consumer::wait() {
  if (!wake_) {
    return;
  }

  fd_set readfds;
  FD_ZERO(&readfds);

  sock_handle max_fd = wake_->read_handle();
  FD_SET(wake_->read_handle(), &readfds);

#if defined(_WIN32)
  const SOCKET active_sock = static_cast<SOCKET>(sock_);
  const SOCKET active_listen = static_cast<SOCKET>(listen_);
  if (active_sock != INVALID_SOCKET) {
    FD_SET(active_sock, &readfds);
  } else if (active_listen != INVALID_SOCKET) {
    FD_SET(active_listen, &readfds);
  }
  const int selected = ::select(0, &readfds, nullptr, nullptr, nullptr);
#else
  if (sock_ >= 0) {
    FD_SET(sock_, &readfds);
    max_fd = std::max(max_fd, sock_);
  } else if (listen_ >= 0) {
    FD_SET(listen_, &readfds);
    max_fd = std::max(max_fd, listen_);
  }
  const int selected = ::select(max_fd + 1, &readfds, nullptr, nullptr, nullptr);
#endif

  if (selected > 0 && FD_ISSET(wake_->read_handle(), &readfds)) {
    wake_->drain();
  }
}
```

- [ ] **Step 8: Run the focused socket wait test**

Run:

```bash
cmake --build build --target xproc_socket_transport_test
ctest --test-dir build -R SocketTransport.WaitBlocksUntilSocketConsumerIsInterrupted --output-on-failure
```

Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add include/xproc/ipc/socket_channel.hpp src/ipc/socket_channel.cpp tests/socket_transport_test.cpp
git commit -m "feat: make socket consumer waits interruptible"
```

## Task 3: Add Explicit Producer Reconnect

**Files:**
- Modify: `include/xproc/ipc/socket_channel.hpp`
- Modify: `src/ipc/socket_channel.cpp`
- Test: `tests/socket_transport_test.cpp`

- [ ] **Step 1: Write producer reconnect tests**

Add these tests to `tests/socket_transport_test.cpp` after `ReconnectAfterPeerDisconnect`:

```cpp
TEST(SocketTransport, ProducerReconnectClosesOldPeerAndSendsOnNewConnection) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::fixed;
    co.item_size = sizeof(std::uint32_t);
    co.socket_host.clear();
    xproc::ipc::socket_consumer cons(co);

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = cons.options().socket_port;
    po.type = xproc::ipc::channel_type::fixed;
    po.item_size = sizeof(std::uint32_t);
    po.socket_connect_retries = 5;
    po.socket_connect_retry_ms = 1;

    xproc::ipc::socket_producer prod(po);
    ASSERT_TRUE(prod.is_connected());

    prod.reconnect();
    ASSERT_TRUE(prod.is_connected());

    const std::uint32_t expected = 0xABCDEF12u;
    prod.send_fixed_sized(&expected, sizeof(expected));

    std::uint32_t actual = 0;
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        ASSERT_EQ(len, sizeof(actual));
        std::memcpy(&actual, p, sizeof(actual));
      });
    }));
    EXPECT_EQ(actual, expected);
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}

TEST(SocketTransport, TryReconnectReturnsFalseWithoutListener) {
  try {
    std::unique_ptr<xproc::ipc::socket_producer> prod;
    std::uint16_t port = 0;

    {
      xproc::ipc::transport_options co;
      co.backend = xproc::ipc::transport_backend::socket;
      co.socket_listen = true;
      co.socket_port = 0;
      co.type = xproc::ipc::channel_type::fixed;
      co.item_size = sizeof(std::uint32_t);
      co.socket_host.clear();
      xproc::ipc::socket_consumer cons(co);
      port = cons.options().socket_port;

      xproc::ipc::transport_options po;
      po.backend = xproc::ipc::transport_backend::socket;
      po.socket_listen = false;
      po.socket_host = "127.0.0.1";
      po.socket_port = port;
      po.type = xproc::ipc::channel_type::fixed;
      po.item_size = sizeof(std::uint32_t);
      po.socket_connect_retries = 1;
      po.socket_connect_retry_ms = 1;
      prod = std::make_unique<xproc::ipc::socket_producer>(po);
    }

    ASSERT_NE(prod, nullptr);
    EXPECT_FALSE(prod->try_reconnect());
    EXPECT_FALSE(prod->is_connected());
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}
```

- [ ] **Step 2: Run the focused tests and verify they fail**

Run:

```bash
cmake --build build --target xproc_socket_transport_test
ctest --test-dir build -R "SocketTransport.(ProducerReconnectClosesOldPeerAndSendsOnNewConnection|TryReconnectReturnsFalseWithoutListener)" --output-on-failure
```

Expected: compile failure because `socket_producer` does not expose `is_connected()`, `reconnect()`, or `try_reconnect()`.

- [ ] **Step 3: Add producer API declarations**

In `include/xproc/ipc/socket_channel.hpp`, add public methods after `options()`:

```cpp
  bool is_connected() const noexcept;
  void reconnect();
  bool try_reconnect() noexcept;
```

Add a private helper declaration:

```cpp
  void connect_with_retries();
```

- [ ] **Step 4: Implement reusable connect and producer state**

In `src/ipc/socket_channel.cpp`, replace the constructor connect loop with:

```cpp
socket_producer::socket_producer(const transport_options& opts) : opts_(opts) {
  validate_producer_transport_options(opts_);
  if (opts_.backend != transport_backend::socket) {
    throw std::logic_error("socket_producer: backend must be socket");
  }
  connect_with_retries();
}
```

Add these methods after the constructor:

```cpp
bool socket_producer::is_connected() const noexcept {
#if defined(_WIN32)
  return sock_ != static_cast<std::uintptr_t>(INVALID_SOCKET);
#else
  return sock_ >= 0;
#endif
}

void socket_producer::connect_with_retries() {
  const int max_retries = (opts_.socket_connect_retries > 0) ? opts_.socket_connect_retries : INT_MAX;
  const int retry_ms = (opts_.socket_connect_retry_ms > 0) ? opts_.socket_connect_retry_ms : 10;
  for (int attempt = 0; attempt < max_retries; ++attempt) {
    try {
#if defined(_WIN32)
      sock_ = static_cast<std::uintptr_t>(tcp_connect(opts_.socket_host, opts_.socket_port));
#else
      sock_ = tcp_connect(opts_.socket_host, opts_.socket_port);
#endif
      return;
    } catch (...) {
      if (attempt + 1 >= max_retries) {
        throw;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(retry_ms));
    }
  }
}

void socket_producer::reconnect() {
  close_sock();
  connect_with_retries();
}

bool socket_producer::try_reconnect() noexcept {
  try {
    reconnect();
    return true;
  } catch (...) {
    close_sock();
    return false;
  }
}
```

- [ ] **Step 5: Close stale sockets on write failure and avoid SIGPIPE**

Change POSIX `write_full_sock()` to pass no-signal flags:

```cpp
    const ssize_t r = ::send(s, p, left, send_no_signal_flags());
```

Change `socket_producer::write_full()` to close the socket before rethrowing:

```cpp
void socket_producer::write_full(const void* data, std::size_t len) {
  try {
#if defined(_WIN32)
    write_full_sock(static_cast<SOCKET>(sock_), data, len);
#else
    write_full_sock(sock_, data, len);
#endif
  } catch (...) {
    close_sock();
    throw;
  }
}
```

- [ ] **Step 6: Run the focused reconnect tests**

Run:

```bash
cmake --build build --target xproc_socket_transport_test
ctest --test-dir build -R "SocketTransport.(ProducerReconnectClosesOldPeerAndSendsOnNewConnection|TryReconnectReturnsFalseWithoutListener)" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add include/xproc/ipc/socket_channel.hpp src/ipc/socket_channel.cpp tests/socket_transport_test.cpp
git commit -m "feat: add explicit socket producer reconnect"
```

## Task 4: Add Partial-Frame Consumer Recovery Coverage

**Files:**
- Modify: `tests/socket_transport_test.cpp`
- Modify: `src/ipc/socket_channel.cpp` only if the new tests expose a stale-peer cleanup gap

- [ ] **Step 1: Add raw socket test helpers**

Add platform socket includes to `tests/socket_transport_test.cpp` after the existing standard includes:

```cpp
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif
```

Add these helper functions inside the anonymous namespace:

```cpp
#if defined(_WIN32)
using test_sock_handle = SOCKET;
test_sock_handle invalid_test_sock() { return INVALID_SOCKET; }
bool test_sock_invalid(test_sock_handle s) { return s == INVALID_SOCKET; }
void close_test_sock(test_sock_handle s) {
  if (!test_sock_invalid(s)) {
    ::closesocket(s);
  }
}
void ensure_test_winsock() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA w{};
    ASSERT_EQ(::WSAStartup(MAKEWORD(2, 2), &w), 0);
  });
}
#else
using test_sock_handle = int;
test_sock_handle invalid_test_sock() { return -1; }
bool test_sock_invalid(test_sock_handle s) { return s < 0; }
void close_test_sock(test_sock_handle s) {
  if (!test_sock_invalid(s)) {
    ::close(s);
  }
}
void ensure_test_winsock() {}
#endif

void raw_socket_send_and_close(std::uint16_t port, const void* data, std::size_t len) {
  ensure_test_winsock();
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* res = nullptr;
  ASSERT_EQ(::getaddrinfo("127.0.0.1", std::to_string(port).c_str(), &hints, &res), 0);
  ASSERT_NE(res, nullptr);

  test_sock_handle sock = invalid_test_sock();
  for (addrinfo* it = res; it != nullptr; it = it->ai_next) {
    sock = static_cast<test_sock_handle>(::socket(it->ai_family, it->ai_socktype, it->ai_protocol));
    if (test_sock_invalid(sock)) {
      continue;
    }
    if (::connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
      break;
    }
    close_test_sock(sock);
    sock = invalid_test_sock();
  }
  ::freeaddrinfo(res);
  ASSERT_FALSE(test_sock_invalid(sock));

  if (len > 0) {
#if defined(_WIN32)
    ASSERT_GT(::send(sock, static_cast<const char*>(data), static_cast<int>(len), 0), 0);
#else
    ASSERT_GT(::send(sock, data, len, 0), 0);
#endif
  }
  close_test_sock(sock);
}
```

- [ ] **Step 2: Add fixed partial-frame recovery test**

Add this test:

```cpp
TEST(SocketTransport, ConsumerRecoversAfterPartialFixedFrameDisconnect) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::fixed;
    co.item_size = sizeof(std::uint32_t);
    co.socket_host.clear();
    xproc::ipc::socket_consumer cons(co);

    const std::uint16_t port = cons.options().socket_port;
    const std::uint16_t partial = 0xBEEF;
    raw_socket_send_and_close(port, &partial, sizeof(partial));

    ASSERT_TRUE(spin_until([&] { return !cons.poll([](void*, std::uint32_t) {}); }));

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = port;
    po.type = xproc::ipc::channel_type::fixed;
    po.item_size = sizeof(std::uint32_t);
    xproc::ipc::socket_producer prod(po);

    const std::uint32_t expected = 0x12345678u;
    prod.send_fixed_sized(&expected, sizeof(expected));

    std::uint32_t actual = 0;
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        ASSERT_EQ(len, sizeof(actual));
        std::memcpy(&actual, p, sizeof(actual));
      });
    }));
    EXPECT_EQ(actual, expected);
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}
```

- [ ] **Step 3: Add varlen partial-frame recovery test**

Add this test:

```cpp
TEST(SocketTransport, ConsumerRecoversAfterPartialVarlenFrameDisconnect) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::varlen;
    co.socket_host.clear();
    xproc::ipc::socket_consumer cons(co);

    const std::uint16_t port = cons.options().socket_port;
    const std::array<std::uint8_t, 5> partial{{5u, 0u, 0u, 0u, 'h'}};
    raw_socket_send_and_close(port, partial.data(), partial.size());

    ASSERT_TRUE(spin_until([&] { return !cons.poll([](void*, std::uint32_t) {}); }));

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = port;
    po.type = xproc::ipc::channel_type::varlen;
    xproc::ipc::socket_producer prod(po);

    const char* expected = "after-partial";
    prod.send_varlen(expected, static_cast<std::uint32_t>(std::strlen(expected)));

    std::string actual;
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        actual.assign(static_cast<const char*>(p), static_cast<std::size_t>(len));
      });
    }));
    EXPECT_EQ(actual, expected);
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}
```

- [ ] **Step 4: Run the partial-frame tests**

Run:

```bash
cmake --build build --target xproc_socket_transport_test
ctest --test-dir build -R "SocketTransport.ConsumerRecoversAfterPartial(Fixed|Varlen)FrameDisconnect" --output-on-failure
```

Expected: PASS. If either test fails because the stale peer remains connected, update the existing `catch (const std::runtime_error&)` path in `socket_consumer::poll_impl()` so it always calls `close_sock()` and returns `false`.

- [ ] **Step 5: Commit**

```bash
git add tests/socket_transport_test.cpp src/ipc/socket_channel.cpp
git commit -m "test: cover socket partial-frame reconnect recovery"
```

## Task 5: Add C API Send-Failure Mapping Coverage

**Files:**
- Modify: `tests/capi_smoke_test.cpp`

- [ ] **Step 1: Add raw reset server helper**

Add platform socket includes to `tests/capi_smoke_test.cpp` after the current includes:

```cpp
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif
```

Add this helper inside the anonymous namespace:

```cpp
class ResettingTcpServer {
 public:
  ResettingTcpServer() {
#if defined(_WIN32)
    static std::once_flag once;
    std::call_once(once, [] {
      WSADATA w{};
      ASSERT_EQ(::WSAStartup(MAKEWORD(2, 2), &w), 0);
    });
    listen_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(listen_, INVALID_SOCKET);
#else
    listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_, 0);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_, 1), 0);

    sockaddr_in bound{};
#if defined(_WIN32)
    int len = sizeof(bound);
#else
    socklen_t len = sizeof(bound);
#endif
    ASSERT_EQ(::getsockname(listen_, reinterpret_cast<sockaddr*>(&bound), &len), 0);
    port_ = ntohs(bound.sin_port);

    worker_ = std::thread([this] {
      auto accepted = ::accept(listen_, nullptr, nullptr);
#if defined(_WIN32)
      if (accepted == INVALID_SOCKET) {
        return;
      }
#else
      if (accepted < 0) {
        return;
      }
#endif
      linger rst{};
      rst.l_onoff = 1;
      rst.l_linger = 0;
      (void)::setsockopt(accepted, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&rst), sizeof(rst));
#if defined(_WIN32)
      ::closesocket(accepted);
#else
      ::close(accepted);
#endif
    });
  }

  ~ResettingTcpServer() {
#if defined(_WIN32)
    if (listen_ != INVALID_SOCKET) {
      ::closesocket(listen_);
    }
#else
    if (listen_ >= 0) {
      ::close(listen_);
    }
#endif
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::uint16_t port() const noexcept { return port_; }

 private:
#if defined(_WIN32)
  SOCKET listen_{INVALID_SOCKET};
#else
  int listen_{-1};
#endif
  std::uint16_t port_{0};
  std::thread worker_;
};
```

- [ ] **Step 2: Add the C API mapping test**

Add this test near the existing socket C API smoke test:

```cpp
TEST(CApiSmoke, SocketProducerSendFailureMapsToRuntimeError) {
  ResettingTcpServer server;

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.backend = XPROC_C_BACKEND_SOCKET;
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);
  producer_opts.socket_host = "127.0.0.1";
  producer_opts.socket_port = server.port();
  producer_opts.socket_listen = 0;
  producer_opts.socket_connect_retries = 1;
  producer_opts.socket_connect_retry_ms = 1;

  xproc_c_producer* producer = nullptr;
  const xproc_c_status open_status = xproc_c_producer_open(&producer_opts, &producer);
  if (open_status != XPROC_C_STATUS_OK) {
    GTEST_SKIP() << "socket producer unavailable in this environment: " << xproc_c_last_error_message();
  }

  const std::uint32_t value = 0xABCD1234u;
  xproc_c_status status = XPROC_C_STATUS_OK;
  for (int i = 0; i < 100; ++i) {
    status = xproc_c_producer_send_fixed_sized(producer, &value, sizeof(value));
    if (status == XPROC_C_STATUS_RUNTIME_ERROR) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_EQ(status, XPROC_C_STATUS_RUNTIME_ERROR);
  xproc_c_producer_close(producer);
}
```

- [ ] **Step 3: Run the focused C API test**

Run:

```bash
cmake --build build --target xproc_capi_smoke_tests
ctest --test-dir build -R CApiSmoke.SocketProducerSendFailureMapsToRuntimeError --output-on-failure
```

Expected: PASS after producer write failures close stale sockets and propagate as runtime errors through `catch_status`.

- [ ] **Step 4: Commit**

```bash
git add tests/capi_smoke_test.cpp
git commit -m "test: cover socket send failure c api mapping"
```

## Task 6: Run Socket and Runtime Verification

**Files:**
- No source edits unless verification exposes a regression

- [ ] **Step 1: Run socket transport tests**

Run:

```bash
cmake --build build --target xproc_socket_transport_test
ctest --test-dir build -R SocketTransport --output-on-failure
```

Expected: PASS or environment-specific GTest skips for unavailable IPv6/socket cases. There must be no failures.

- [ ] **Step 2: Run runtime allocation tests**

Run:

```bash
cmake --build build --target xproc_runtime_allocation_test
ctest --test-dir build -R RuntimeAllocation --output-on-failure
```

Expected: PASS.

- [ ] **Step 3: Run C API smoke tests**

Run:

```bash
cmake --build build --target xproc_capi_smoke_tests
ctest --test-dir build -R CApiSmoke --output-on-failure
```

Expected: PASS or environment-specific GTest skips for unavailable socket cases. There must be no failures.

- [ ] **Step 4: Run formatting and whitespace checks**

Run:

```bash
git diff --check
```

Expected: no output and exit code 0.

- [ ] **Step 5: Commit verification-only fixes if needed**

If the previous steps required minor portability fixes, commit the exact changed files:

```bash
git status --short
git add <changed-files>
git commit -m "fix: stabilize socket reconnect resilience tests"
```

Expected: this step is skipped when `git status --short` is empty after verification.

## Final Verification

- [ ] Run the focused test suite for the feature:

```bash
cmake --build build --target xproc_socket_transport_test xproc_runtime_allocation_test xproc_capi_smoke_tests
ctest --test-dir build -R "SocketTransport|RuntimeAllocation|CApiSmoke" --output-on-failure
```

Expected: all non-skipped tests pass.

- [ ] Inspect the final diff:

```bash
git status --short
git log --oneline -n 8
```

Expected: worktree is clean after all commits; recent commits correspond to the tasks above.
