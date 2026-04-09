#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

void skip_if_socket_unavailable(const std::runtime_error& ex) {
  GTEST_SKIP() << "socket transport unavailable in this environment: " << ex.what();
}

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

}  // namespace

TEST(SocketTransport, VarlenTcpLoopbackIPv4) {
  run_varlen_loopback("127.0.0.1");
}

TEST(SocketTransport, FixedTcpLoopbackIPv6) {
  run_fixed_loopback("::1");
}

TEST(SocketTransport, FactoryCreatesShmAndSocket) {
  xproc::ipc::transport_options shm_o;
  shm_o.backend = xproc::ipc::transport_backend::shared_memory;
  shm_o.path = "/xproc_socket_test_factory_shm";
  shm_o.shm_size = sizeof(xproc::shm::control_block) + 4096;
  shm_o.type = xproc::ipc::channel_type::fixed;
  shm_o.item_size = 4;
  xproc::shm::shm::unlink(shm_o.path);
  auto prod = xproc::ipc::create_producer_transport(shm_o);
  ASSERT_NE(prod, nullptr);
  EXPECT_NE(prod->shared_header(), nullptr);
  xproc::shm::shm::unlink(shm_o.path);
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
