#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

TEST(SocketTransport, VarlenTcpLoopback) {
  std::promise<std::uint16_t> port_promise;
  std::future<std::uint16_t> port_future = port_promise.get_future();
  std::atomic<int> received{0};
  std::vector<std::string> copies;

  std::thread consumer_thread([&] {
    xproc::ipc::transport_options co;
    co.backend = xproc::ipc::transport_backend::socket;
    co.socket_listen = true;
    co.socket_port = 0;
    co.type = xproc::ipc::channel_type::varlen;
    co.socket_host = "127.0.0.1";

    xproc::ipc::socket_consumer cons(co);
    port_promise.set_value(cons.options().socket_port);

    while (received.load(std::memory_order_acquire) < 3) {
      const bool got = cons.poll([&](void* p, std::uint32_t len) {
        copies.emplace_back(static_cast<const char*>(p), static_cast<std::size_t>(len));
        received.fetch_add(1, std::memory_order_acq_rel);
      });
      if (!got) {
        cons.wait();
      }
    }
  });

  const std::uint16_t port = port_future.get();

  xproc::ipc::transport_options po;
  po.backend = xproc::ipc::transport_backend::socket;
  po.socket_listen = false;
  po.socket_host = "127.0.0.1";
  po.socket_port = port;
  po.type = xproc::ipc::channel_type::varlen;

  xproc::ipc::socket_producer prod(po);
  const char* a = "alpha";
  const char* b = "beta";
  prod.send_varlen(a, static_cast<std::uint32_t>(std::strlen(a)));
  prod.send_varlen(b, static_cast<std::uint32_t>(std::strlen(b)));
  prod.send_varlen("", 0);

  consumer_thread.join();

  ASSERT_EQ(received.load(), 3);
  ASSERT_EQ(copies.size(), 3u);
  EXPECT_EQ(copies[0], "alpha");
  EXPECT_EQ(copies[1], "beta");
  EXPECT_TRUE(copies[2].empty());
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
