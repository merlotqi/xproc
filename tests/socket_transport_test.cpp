#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

#if defined(_WIN32)
using test_sock_handle = SOCKET;
constexpr test_sock_handle k_invalid_test_sock = INVALID_SOCKET;
#else
using test_sock_handle = int;
constexpr test_sock_handle k_invalid_test_sock = -1;
#endif

void skip_if_socket_unavailable(const std::runtime_error& ex) {
  GTEST_SKIP() << "socket transport unavailable in this environment: " << ex.what();
}

bool ensure_test_winsock() {
#if defined(_WIN32)
  static const int startup_result = [] {
    WSADATA data;
    return ::WSAStartup(MAKEWORD(2, 2), &data);
  }();
  return startup_result == 0;
#else
  return true;
#endif
}

void close_test_sock(test_sock_handle sock) noexcept {
#if defined(_WIN32)
  if (sock != INVALID_SOCKET) {
    ::closesocket(sock);
  }
#else
  if (sock >= 0) {
    ::close(sock);
  }
#endif
}

void set_test_sock_linger_reset(test_sock_handle sock) noexcept {
  linger reset{};
  reset.l_onoff = 1;
  reset.l_linger = 0;
  static_cast<void>(
      ::setsockopt(sock, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&reset), sizeof(reset)));
}

void raw_socket_send_and_close(std::uint16_t port, const void* data, std::size_t len) {
  ASSERT_TRUE(ensure_test_winsock());

  const std::string port_text = std::to_string(port);
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* resolved = nullptr;
  ASSERT_EQ(::getaddrinfo("127.0.0.1", port_text.c_str(), &hints, &resolved), 0);
  struct addrinfo_guard {
    addrinfo* value;
    ~addrinfo_guard() {
      if (value != nullptr) {
        ::freeaddrinfo(value);
      }
    }
  } guard{resolved};

  test_sock_handle sock = k_invalid_test_sock;
  struct sock_guard {
    test_sock_handle& value;
    ~sock_guard() { close_test_sock(value); }
  } closer{sock};

  for (addrinfo* ai = resolved; ai != nullptr; ai = ai->ai_next) {
    sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock == k_invalid_test_sock) {
      continue;
    }
#if defined(_WIN32)
    const int addr_len = static_cast<int>(ai->ai_addrlen);
#else
    const socklen_t addr_len = static_cast<socklen_t>(ai->ai_addrlen);
#endif
    if (::connect(sock, ai->ai_addr, addr_len) == 0) {
      break;
    }
    close_test_sock(sock);
    sock = k_invalid_test_sock;
  }

  ASSERT_NE(sock, k_invalid_test_sock) << "raw socket connect failed";

  const char* bytes = static_cast<const char*>(data);
  std::size_t sent = 0;
  while (sent < len) {
#if defined(_WIN32)
    const int n = ::send(sock, bytes + sent, static_cast<int>(len - sent), 0);
#else
    const ssize_t n = ::send(sock, bytes + sent, len - sent, 0);
#endif
    ASSERT_GT(n, 0) << "raw socket send failed";
    sent += static_cast<std::size_t>(n);
  }
}

class ResettingTcpServer {
 public:
  ResettingTcpServer() {
    if (!ensure_test_winsock()) {
      throw std::runtime_error("WSAStartup failed");
    }

    test_sock_handle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == k_invalid_test_sock) {
      throw std::runtime_error("socket failed");
    }

    const int reuse = 1;
    static_cast<void>(
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
      close_test_sock(listener);
      throw std::runtime_error("bind failed");
    }
    if (::listen(listener, 1) != 0) {
      close_test_sock(listener);
      throw std::runtime_error("listen failed");
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int bound_len = sizeof(bound);
#else
    socklen_t bound_len = sizeof(bound);
#endif
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
      close_test_sock(listener);
      throw std::runtime_error("getsockname failed");
    }

    listener_ = listener;
    port_ = ntohs(bound.sin_port);

    try {
      worker_ = std::thread([this] { accept_and_reset(); });
    } catch (...) {
      close_test_sock(listener_);
      listener_ = k_invalid_test_sock;
      throw;
    }
  }

  ResettingTcpServer(const ResettingTcpServer&) = delete;
  ResettingTcpServer& operator=(const ResettingTcpServer&) = delete;

  ~ResettingTcpServer() {
    stop_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
      worker_.join();
    }
    close_test_sock(listener_);
  }

  std::uint16_t port() const noexcept { return port_; }
  bool reset_completed() const noexcept { return reset_completed_.load(std::memory_order_acquire); }

 private:
  void accept_and_reset() noexcept {
    while (!stop_.load(std::memory_order_acquire)) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(listener_, &readfds);

      timeval timeout{};
      timeout.tv_usec = 10 * 1000;

#if defined(_WIN32)
      const int ready = ::select(0, &readfds, nullptr, nullptr, &timeout);
#else
      const int ready = ::select(listener_ + 1, &readfds, nullptr, nullptr, &timeout);
#endif
      if (ready == 0) {
        continue;
      }
      if (ready < 0) {
#if !defined(_WIN32)
        if (errno == EINTR) {
          continue;
        }
#endif
        return;
      }

      test_sock_handle accepted = ::accept(listener_, nullptr, nullptr);
      if (accepted == k_invalid_test_sock) {
        continue;
      }

      set_test_sock_linger_reset(accepted);
      close_test_sock(accepted);
      reset_completed_.store(true, std::memory_order_release);
      return;
    }
  }

  test_sock_handle listener_ = k_invalid_test_sock;
  std::uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  std::atomic<bool> reset_completed_{false};
  std::thread worker_;
};

void run_varlen_loopback(const std::string& producer_host) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::varlen;
    co.socket_host.clear();

    xproc::ipc::socket_consumer cons(co);

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = producer_host;
    po.socket_port = cons.options().socket_port;
    po.type = xproc::ipc::channel_type::varlen;

    xproc::ipc::socket_producer prod(po);
    const char* a = "alpha";
    const char* b = "beta";
    prod.send_varlen(a, static_cast<std::uint32_t>(std::strlen(a)));
    prod.send_varlen(b, static_cast<std::uint32_t>(std::strlen(b)));
    prod.send_varlen("", 0);

    std::vector<std::string> copies;
    while (copies.size() < 3u) {
      const bool got = cons.poll([&](void* p, std::uint32_t len) {
        copies.emplace_back(static_cast<const char*>(p), static_cast<std::size_t>(len));
      });
      if (!got) {
        cons.wait();
      }
    }

    ASSERT_EQ(copies.size(), 3u);
    EXPECT_EQ(copies[0], "alpha");
    EXPECT_EQ(copies[1], "beta");
    EXPECT_TRUE(copies[2].empty());
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}

void run_fixed_loopback(const std::string& producer_host) {
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
    po.socket_host = producer_host;
    po.socket_port = cons.options().socket_port;
    po.type = xproc::ipc::channel_type::fixed;
    po.item_size = sizeof(std::uint32_t);

    xproc::ipc::socket_producer prod(po);
    const std::array<std::uint32_t, 2> expected{{0x10203040u, 0x55667788u}};
    prod.send_fixed_sized(&expected[0], sizeof(expected[0]));
    prod.send_fixed_sized(&expected[1], sizeof(expected[1]));

    std::vector<std::uint32_t> actual;
    while (actual.size() < expected.size()) {
      const bool got = cons.poll([&](void* p, std::uint32_t len) {
        ASSERT_EQ(len, sizeof(std::uint32_t));
        std::uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        actual.push_back(v);
      });
      if (!got) {
        cons.wait();
      }
    }

    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_EQ(actual[0], expected[0]);
    EXPECT_EQ(actual[1], expected[1]);
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}

template <typename Predicate>
bool spin_until(Predicate&& predicate, int iterations = 4000, int sleep_us = 200) {
  for (int i = 0; i < iterations; ++i) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
  }
  return false;
}

void drive_partial_disconnect_cleanup(xproc::ipc::socket_consumer& cons) {
  std::size_t complete_messages = 0;
  bool unexpected_complete_message = false;
  ASSERT_TRUE(spin_until([&] {
    for (int i = 0; i < 4; ++i) {
      const bool got = cons.poll([&](void*, std::uint32_t) { ++complete_messages; });
      unexpected_complete_message = unexpected_complete_message || got;
    }
    return !cons.is_connected();
  })) << "consumer did not clean up the partial-frame peer";
  EXPECT_FALSE(unexpected_complete_message);
  EXPECT_EQ(complete_messages, 0u);
}

}  // namespace

TEST(SocketTransport, VarlenTcpLoopbackIPv4) { run_varlen_loopback("127.0.0.1"); }

TEST(SocketTransport, FixedTcpLoopbackIPv6) { run_fixed_loopback("::1"); }

TEST(SocketTransport, FixedTcpLoopbackIPv4) { run_fixed_loopback("127.0.0.1"); }

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

TEST(SocketTransport, VarlenTcpLoopbackIpv6BracketHost) { run_varlen_loopback("[::1]"); }

TEST(SocketTransport, ReconnectAfterPeerDisconnect) {
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

    {
      xproc::ipc::socket_producer first(po);
      const std::uint32_t v1 = 0x11112222u;
      first.send_fixed_sized(&v1, sizeof(v1));
    }

    std::vector<std::uint32_t> observed;
    auto poll_once = [&]() -> bool {
      return cons.poll([&](void* p, std::uint32_t len) {
        ASSERT_EQ(len, sizeof(std::uint32_t));
        std::uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        observed.push_back(v);
      });
    };

    ASSERT_TRUE(spin_until([&]() { return poll_once(); }));
    ASSERT_EQ(observed.size(), 1u);
    EXPECT_EQ(observed[0], 0x11112222u);

    {
      xproc::ipc::socket_producer second(po);
      const std::uint32_t v2 = 0x33334444u;
      second.send_fixed_sized(&v2, sizeof(v2));
    }

    ASSERT_TRUE(spin_until([&]() { return poll_once(); }));
    ASSERT_EQ(observed.size(), 2u);
    EXPECT_EQ(observed[1], 0x33334444u);
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}

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

    const std::uint32_t first = 0x10203040u;
    prod.send_fixed_sized(&first, sizeof(first));

    std::uint32_t actual = 0;
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        ASSERT_EQ(len, sizeof(actual));
        std::memcpy(&actual, p, sizeof(actual));
      });
    }));
    EXPECT_EQ(actual, first);

    prod.reconnect();
    ASSERT_TRUE(prod.is_connected());

    const std::uint32_t second = 0xABCDEF12u;
    prod.send_fixed_sized(&second, sizeof(second));

    actual = 0;
    ASSERT_TRUE(spin_until([&] {
      return cons.poll([&](void* p, std::uint32_t len) {
        ASSERT_EQ(len, sizeof(actual));
        std::memcpy(&actual, p, sizeof(actual));
      });
    }));
    EXPECT_EQ(actual, second);
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

TEST(SocketTransport, ProducerSendFailureClosesSocket) {
  try {
    ResettingTcpServer server;

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = server.port();
    po.type = xproc::ipc::channel_type::fixed;
    po.item_size = sizeof(std::uint32_t);
    po.socket_connect_retries = 1;
    po.socket_connect_retry_ms = 1;

    xproc::ipc::socket_producer prod(po);
    ASSERT_TRUE(prod.is_connected());
    ASSERT_TRUE(spin_until([&] { return server.reset_completed(); }, 1000, 1000));

    const std::uint32_t value = 0xCC55AA33u;
    bool failed = false;
    for (int i = 0; i < 100 && !failed; ++i) {
      try {
        prod.send_fixed_sized(&value, sizeof(value));
      } catch (const std::runtime_error&) {
        failed = true;
      }
      if (!failed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }

    EXPECT_TRUE(failed);
    EXPECT_FALSE(prod.is_connected());
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}

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

    const std::uint16_t partial = 0xBEEFu;
    raw_socket_send_and_close(cons.options().socket_port, &partial, sizeof(partial));
    drive_partial_disconnect_cleanup(cons);

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = cons.options().socket_port;
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

TEST(SocketTransport, ConsumerRecoversAfterPartialVarlenFrameDisconnect) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::varlen;
    co.socket_host.clear();
    xproc::ipc::socket_consumer cons(co);

    const std::array<char, 5> partial{{5, 0, 0, 0, 'h'}};
    raw_socket_send_and_close(cons.options().socket_port, partial.data(), partial.size());
    drive_partial_disconnect_cleanup(cons);

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = cons.options().socket_port;
    po.type = xproc::ipc::channel_type::varlen;

    xproc::ipc::socket_producer prod(po);
    const std::string expected = "after-partial";
    prod.send_varlen(expected.data(), static_cast<std::uint32_t>(expected.size()));

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

TEST(SocketTransport, RuntimeProcessesSocketMessages) {
  try {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::varlen;
    co.socket_host.clear();
    xproc::ipc::socket_consumer cons(co);

    xproc::ipc::transport_options po;
    po.backend = xproc::ipc::transport_backend::socket;
    po.socket_listen = false;
    po.socket_host = "127.0.0.1";
    po.socket_port = cons.options().socket_port;
    po.type = xproc::ipc::channel_type::varlen;
    xproc::ipc::socket_producer prod(po);

    xproc::ipc::runtime rt(cons);
    std::atomic<int> handled{0};
    std::mutex mu;
    std::vector<std::string> messages;

    auto executor = [](auto&& task) { task(); };
    std::thread worker([&] {
      rt.run(executor, [&](const std::uint8_t* data, std::size_t len) {
        std::lock_guard<std::mutex> lock(mu);
        messages.emplace_back(reinterpret_cast<const char*>(data), len);
        handled.fetch_add(1, std::memory_order_relaxed);
      });
    });

    const char* m1 = "runtime-a";
    const char* m2 = "runtime-b";
    prod.send_varlen(m1, static_cast<std::uint32_t>(std::strlen(m1)));
    prod.send_varlen(m2, static_cast<std::uint32_t>(std::strlen(m2)));

    ASSERT_TRUE(spin_until([&]() { return handled.load(std::memory_order_relaxed) >= 2; }, 5000, 200));

    rt.stop();
    worker.join();

    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0], "runtime-a");
    EXPECT_EQ(messages[1], "runtime-b");
  } catch (const std::runtime_error& ex) {
    skip_if_socket_unavailable(ex);
  }
}

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

TEST(SocketTransport, FactoryCreatesShmAndSocket) {
  xproc::ipc::transport_options shm_o;
  shm_o.backend = xproc::ipc::transport_backend::shared_memory;
  shm_o.path = "/xproc_socket_test_factory_shm";
  shm_o.shm_size = sizeof(xproc::core::control_block) + 4096;
  shm_o.type = xproc::ipc::channel_type::fixed;
  shm_o.item_size = 4;
  xproc::core::shm::unlink(shm_o.path);
  auto prod = xproc::ipc::create_producer_transport(shm_o);
  ASSERT_NE(prod, nullptr);
  EXPECT_NE(prod->shared_header(), nullptr);
  xproc::core::shm::unlink(shm_o.path);
}

TEST(SocketTransport, ValidateRejectsEmptySocketHost) {
  xproc::ipc::transport_options o;
  o.backend = xproc::ipc::transport_backend::socket;
  o.socket_host.clear();
  o.socket_listen = false;
  o.socket_port = 1;
  o.type = xproc::ipc::channel_type::varlen;
  EXPECT_THROW(xproc::ipc::validate_transport_options(o), std::invalid_argument);
}

TEST(SocketTransport, ValidateAllowsIpv6LiteralHost) {
  xproc::ipc::transport_options o;
  o.backend = xproc::ipc::transport_backend::socket;
  o.socket_host = "::1";
  o.socket_listen = false;
  o.socket_port = 1;
  o.type = xproc::ipc::channel_type::varlen;
  EXPECT_NO_THROW(xproc::ipc::validate_transport_options(o));
}

TEST(SocketTransport, ValidateAllowsEmptySocketHostWhenListening) {
  xproc::ipc::transport_options o;
  o.backend = xproc::ipc::transport_backend::socket;
  o.socket_listen = true;
  o.socket_host.clear();
  o.socket_port = 0;
  o.type = xproc::ipc::channel_type::varlen;
  EXPECT_NO_THROW(xproc::ipc::validate_transport_options(o));
  EXPECT_NO_THROW(xproc::ipc::validate_consumer_transport_options(o));
}

TEST(SocketTransport, ValidateRejectsConnectModeWithoutPort) {
  xproc::ipc::transport_options o;
  o.backend = xproc::ipc::transport_backend::socket;
  o.socket_listen = false;
  o.socket_host = "127.0.0.1";
  o.socket_port = 0;
  o.type = xproc::ipc::channel_type::varlen;
  EXPECT_THROW(xproc::ipc::validate_transport_options(o), std::invalid_argument);
}

TEST(SocketTransport, ValidateRejectsNegativeRetryBounds) {
  xproc::ipc::transport_options retries;
  retries.backend = xproc::ipc::transport_backend::socket;
  retries.socket_listen = false;
  retries.socket_host = "127.0.0.1";
  retries.socket_port = 7;
  retries.type = xproc::ipc::channel_type::varlen;
  retries.socket_connect_retries = -1;
  EXPECT_THROW(xproc::ipc::validate_transport_options(retries), std::invalid_argument);

  xproc::ipc::transport_options retry_ms = retries;
  retry_ms.socket_connect_retries = 1;
  retry_ms.socket_connect_retry_ms = -1;
  EXPECT_THROW(xproc::ipc::validate_transport_options(retry_ms), std::invalid_argument);
}

TEST(SocketTransport, RoleValidationRejectsSocketListenMismatch) {
  xproc::ipc::transport_options o;
  o.backend = xproc::ipc::transport_backend::socket;
  o.socket_host = "127.0.0.1";
  o.socket_port = 9;
  o.type = xproc::ipc::channel_type::varlen;

  o.socket_listen = true;
  EXPECT_THROW(xproc::ipc::validate_producer_transport_options(o), std::invalid_argument);

  o.socket_listen = false;
  EXPECT_THROW(xproc::ipc::validate_consumer_transport_options(o), std::invalid_argument);
}

TEST(SocketTransport, ObserverValidationRejectsSocketBackend) {
  xproc::ipc::transport_options o;
  o.backend = xproc::ipc::transport_backend::socket;
  o.socket_listen = true;
  o.socket_port = 0;
  o.type = xproc::ipc::channel_type::varlen;
  EXPECT_THROW(xproc::ipc::validate_observer_transport_options(o), std::invalid_argument);
}
