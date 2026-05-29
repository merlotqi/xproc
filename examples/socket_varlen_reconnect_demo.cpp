// Socket variable-length reconnect example (single process, loopback TCP).
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

xproc::ipc::transport_options make_socket_consumer_options() {
  xproc::ipc::transport_options opts;
  opts.backend = xproc::ipc::transport_backend::socket;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.socket_listen = true;
  opts.socket_port = 0;
  opts.socket_host.clear();
  return opts;
}

xproc::ipc::transport_options make_socket_producer_options(std::uint16_t port) {
  xproc::ipc::transport_options opts;
  opts.backend = xproc::ipc::transport_backend::socket;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.socket_listen = false;
  opts.socket_host = "127.0.0.1";
  opts.socket_port = port;
  opts.socket_connect_retries = 50;
  opts.socket_connect_retry_ms = 2;
  return opts;
}

bool poll_until(xproc::ipc::socket_consumer& consumer, const std::string& expected) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    bool matched = false;
    const bool got = consumer.poll([&](void* data, std::uint32_t len) {
      const std::string message(static_cast<const char*>(data), static_cast<std::size_t>(len));
      std::cout << "consumer received: " << message << "\n";
      matched = (message == expected);
    });
    if (got && matched) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool wait_until_consumer_drops_stale_peer(xproc::ipc::socket_consumer& consumer) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (!consumer.is_connected()) {
      return true;
    }
    const bool got_message = consumer.poll([](void*, std::uint32_t) {});
    if (!got_message && !consumer.is_connected()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return !consumer.is_connected();
}

}  // namespace

int main() {
  try {
    xproc::ipc::socket_consumer consumer(make_socket_consumer_options());
    xproc::ipc::socket_producer producer(make_socket_producer_options(consumer.options().socket_port));

    if (!producer.is_connected()) {
      std::cerr << "producer did not connect to socket consumer\n";
      return 1;
    }

    const std::vector<std::string> messages = {"before reconnect", "after reconnect"};

    producer.send_varlen(messages[0].data(), static_cast<std::uint32_t>(messages[0].size()));
    if (!poll_until(consumer, messages[0])) {
      std::cerr << "timed out waiting for first socket message\n";
      return 1;
    }

    std::cout << "producer reconnecting; old peer should be discarded by consumer\n";
    producer.reconnect();
    if (!producer.is_connected()) {
      std::cerr << "producer reconnect did not establish a socket connection\n";
      return 1;
    }

    if (!wait_until_consumer_drops_stale_peer(consumer)) {
      std::cerr << "timed out waiting for consumer to drop the stale socket peer\n";
      return 1;
    }
    std::cout << "consumer dropped stale peer and is ready for the reconnected producer\n";

    producer.send_varlen(messages[1].data(), static_cast<std::uint32_t>(messages[1].size()));
    if (!poll_until(consumer, messages[1])) {
      std::cerr << "timed out waiting for reconnected socket message\n";
      return 1;
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "socket reconnect demo failed: " << ex.what() << "\n";
    return 1;
  }
}
