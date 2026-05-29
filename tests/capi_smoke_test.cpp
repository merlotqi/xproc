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
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <xproc_c.h>
}

#include <xproc/core/shm.hpp>
#include <xproc/core/shm_layout.hpp>

namespace {

#if defined(_WIN32)
using test_sock_handle = SOCKET;
constexpr test_sock_handle k_invalid_test_sock = INVALID_SOCKET;
#else
using test_sock_handle = int;
constexpr test_sock_handle k_invalid_test_sock = -1;
#endif

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
    stop_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
    close_test_sock(listener_);
  }

  std::uint16_t port() const noexcept { return port_; }

 private:
  void accept_and_reset() noexcept {
    while (!stop_.load()) {
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
      return;
    }
  }

  test_sock_handle listener_ = k_invalid_test_sock;
  std::uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

}  // namespace

TEST(CApiSmoke, FixedProducerConsumerRoundTrip) {
  const std::string path = "/xproc_capi_fixed_roundtrip";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.path = path.c_str();
  producer_opts.shm_size = xproc_c_shm_size_for_data_capacity(8192);
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);
  producer_opts.schema_id = 0x0102030405060708ull;

  xproc_c_options consumer_opts = producer_opts;
  consumer_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  consumer_opts.create_if_missing = 0;

  xproc_c_producer* producer = nullptr;
  xproc_c_consumer* consumer = nullptr;

  ASSERT_EQ(xproc_c_producer_open(&producer_opts, &producer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_consumer_open(&consumer_opts, &consumer), XPROC_C_STATUS_OK);

  xproc_c_options borrowed_producer{};
  xproc_c_options borrowed_consumer{};
  ASSERT_EQ(xproc_c_producer_options(producer, &borrowed_producer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_consumer_options(consumer, &borrowed_consumer), XPROC_C_STATUS_OK);
  ASSERT_NE(borrowed_producer.path, nullptr);
  ASSERT_NE(borrowed_consumer.path, nullptr);
  EXPECT_STREQ(borrowed_producer.path, path.c_str());
  EXPECT_STREQ(borrowed_consumer.path, path.c_str());
  EXPECT_EQ(borrowed_producer.channel_type, XPROC_C_CHANNEL_FIXED);
  EXPECT_EQ(borrowed_consumer.channel_type, XPROC_C_CHANNEL_FIXED);
  EXPECT_EQ(borrowed_producer.item_size, sizeof(std::uint32_t));
  EXPECT_EQ(borrowed_consumer.item_size, sizeof(std::uint32_t));
  EXPECT_EQ(borrowed_producer.schema_id, producer_opts.schema_id);
  EXPECT_EQ(borrowed_consumer.schema_id, producer_opts.schema_id);
  EXPECT_EQ(borrowed_consumer.shm_size, producer_opts.shm_size);

  const std::uint32_t expected = 0x12345678u;
  ASSERT_EQ(xproc_c_producer_send_fixed_sized(producer, &expected, sizeof(expected)), XPROC_C_STATUS_OK);

  std::uint32_t actual = 0;
  std::uint32_t out_len = 0;
  ASSERT_EQ(xproc_c_consumer_poll_copy(consumer, &actual, sizeof(actual), &out_len), XPROC_C_STATUS_OK);
  EXPECT_EQ(out_len, sizeof(actual));
  EXPECT_EQ(actual, expected);

  xproc_c_consumer_close(consumer);
  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

TEST(CApiSmoke, VarlenBufferTooSmallRetainsMessage) {
  const std::string path = "/xproc_capi_varlen_pending";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.path = path.c_str();
  producer_opts.shm_size = xproc_c_shm_size_for_data_capacity(8192);
  producer_opts.channel_type = XPROC_C_CHANNEL_VARLEN;
  producer_opts.schema_id = 0xBEEF;

  xproc_c_options consumer_opts = producer_opts;
  consumer_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  consumer_opts.create_if_missing = 0;

  xproc_c_producer* producer = nullptr;
  xproc_c_consumer* consumer = nullptr;

  ASSERT_EQ(xproc_c_producer_open(&producer_opts, &producer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_consumer_open(&consumer_opts, &consumer), XPROC_C_STATUS_OK);

  const std::array<std::uint8_t, 6> expected{{1u, 2u, 3u, 4u, 5u, 6u}};
  ASSERT_EQ(xproc_c_producer_send_varlen(producer, expected.data(), static_cast<std::uint32_t>(expected.size())),
            XPROC_C_STATUS_OK);

  std::array<std::uint8_t, 2> too_small{};
  std::uint32_t out_len = 0;
  EXPECT_EQ(
      xproc_c_consumer_poll_copy(consumer, too_small.data(), static_cast<std::uint32_t>(too_small.size()), &out_len),
      XPROC_C_STATUS_BUFFER_TOO_SMALL);
  EXPECT_EQ(out_len, expected.size());

  std::uint32_t pending_len = 0;
  ASSERT_EQ(xproc_c_consumer_pending_len(consumer, &pending_len), XPROC_C_STATUS_OK);
  EXPECT_EQ(pending_len, expected.size());
  EXPECT_EQ(xproc_c_consumer_wait(consumer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_consumer_pending_len(consumer, &pending_len), XPROC_C_STATUS_OK);
  EXPECT_EQ(pending_len, expected.size());

  std::array<std::uint8_t, 6> actual{};
  ASSERT_EQ(xproc_c_consumer_poll_copy(consumer, actual.data(), static_cast<std::uint32_t>(actual.size()), &out_len),
            XPROC_C_STATUS_OK);
  EXPECT_EQ(out_len, expected.size());
  EXPECT_EQ(actual, expected);
  ASSERT_EQ(xproc_c_consumer_pending_len(consumer, &pending_len), XPROC_C_STATUS_OK);
  EXPECT_EQ(pending_len, 0u);

  xproc_c_consumer_close(consumer);
  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

TEST(CApiSmoke, ObserverSnapshotAndPeek) {
  const std::string path = "/xproc_capi_observer";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.path = path.c_str();
  producer_opts.shm_size = xproc_c_shm_size_for_data_capacity(8192);
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);
  producer_opts.schema_id = 0xAABBCCDDEEFF0011ull;

  xproc_c_options observer_opts = producer_opts;
  observer_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  observer_opts.create_if_missing = 0;

  xproc_c_producer* producer = nullptr;
  xproc_c_observer* observer = nullptr;

  ASSERT_EQ(xproc_c_producer_open(&producer_opts, &producer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_observer_open(&observer_opts, &observer), XPROC_C_STATUS_OK);

  const std::uint32_t expected = 0xAABBCCDDu;
  ASSERT_EQ(xproc_c_producer_send_fixed_sized(producer, &expected, sizeof(expected)), XPROC_C_STATUS_OK);

  xproc_c_snapshot snapshot{};
  ASSERT_EQ(xproc_c_observer_snapshot(observer, &snapshot), XPROC_C_STATUS_OK);
  EXPECT_GE(snapshot.attach_count, 1u);

  xproc_c_options borrowed{};
  ASSERT_EQ(xproc_c_observer_options(observer, &borrowed), XPROC_C_STATUS_OK);
  ASSERT_NE(borrowed.path, nullptr);
  EXPECT_STREQ(borrowed.path, path.c_str());
  EXPECT_EQ(borrowed.channel_type, XPROC_C_CHANNEL_FIXED);
  EXPECT_EQ(borrowed.schema_id, producer_opts.schema_id);
  EXPECT_EQ(borrowed.shm_size, producer_opts.shm_size);

  std::uint32_t actual = 0;
  std::uint32_t out_len = 0;
  ASSERT_EQ(xproc_c_observer_peek_copy(observer, &actual, sizeof(actual), &out_len), XPROC_C_STATUS_OK);
  EXPECT_EQ(out_len, sizeof(actual));
  EXPECT_EQ(actual, expected);

  xproc_c_observer_close(observer);
  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

TEST(CApiSmoke, CreatorMetadataDefaultsAndAttachReturnsPersistedValues) {
  xproc_c_options defaults{};
  xproc_c_options_init(&defaults);
  EXPECT_EQ(defaults.creator_timestamp_ns, 0u);
  EXPECT_EQ(defaults.creator_flags, 0u);

  const std::string path = "/xproc_capi_creator_metadata_roundtrip";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  constexpr std::uint64_t persisted_timestamp = 0x1122334455667788ull;
  constexpr std::uint64_t persisted_flags = 0x8877665544332211ull;

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.path = path.c_str();
  producer_opts.shm_size = xproc_c_shm_size_for_data_capacity(4096);
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);
  producer_opts.creator_timestamp_ns = persisted_timestamp;
  producer_opts.creator_flags = persisted_flags;

  ASSERT_EQ(xproc_c_validate_options_for(XPROC_C_ENDPOINT_PRODUCER, &producer_opts), XPROC_C_STATUS_OK);

  xproc_c_producer* producer = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&producer_opts, &producer), XPROC_C_STATUS_OK);

  xproc_c_options borrowed_producer{};
  ASSERT_EQ(xproc_c_producer_options(producer, &borrowed_producer), XPROC_C_STATUS_OK);
  EXPECT_EQ(borrowed_producer.creator_timestamp_ns, persisted_timestamp);
  EXPECT_EQ(borrowed_producer.creator_flags, persisted_flags);

  xproc_c_options consumer_opts = producer_opts;
  consumer_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  consumer_opts.create_if_missing = 0;
  consumer_opts.creator_timestamp_ns = 0xA1A2A3A4A5A6A7A8ull;
  consumer_opts.creator_flags = 0xB1B2B3B4B5B6B7B8ull;

  xproc_c_options observer_opts = producer_opts;
  observer_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  observer_opts.create_if_missing = 0;
  observer_opts.creator_timestamp_ns = 0x0102030405060708ull;
  observer_opts.creator_flags = 0x1112131415161718ull;

  xproc_c_consumer* consumer = nullptr;
  xproc_c_observer* observer = nullptr;
  ASSERT_EQ(xproc_c_consumer_open(&consumer_opts, &consumer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_observer_open(&observer_opts, &observer), XPROC_C_STATUS_OK);

  xproc_c_options borrowed_consumer{};
  xproc_c_options borrowed_observer{};
  ASSERT_EQ(xproc_c_consumer_options(consumer, &borrowed_consumer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_observer_options(observer, &borrowed_observer), XPROC_C_STATUS_OK);

  EXPECT_EQ(borrowed_consumer.creator_timestamp_ns, persisted_timestamp);
  EXPECT_EQ(borrowed_consumer.creator_flags, persisted_flags);
  EXPECT_EQ(borrowed_observer.creator_timestamp_ns, persisted_timestamp);
  EXPECT_EQ(borrowed_observer.creator_flags, persisted_flags);
  EXPECT_EQ(borrowed_consumer.shm_size, producer_opts.shm_size);
  EXPECT_EQ(borrowed_observer.shm_size, producer_opts.shm_size);

  const std::uint32_t expected = 0x13579BDFu;
  ASSERT_EQ(xproc_c_producer_send_fixed_sized(producer, &expected, sizeof(expected)), XPROC_C_STATUS_OK);

  std::uint32_t observed = 0;
  std::uint32_t observed_len = 0;

  xproc_c_status observer_status = XPROC_C_STATUS_AGAIN;
  for (int i = 0; i < 100 && observer_status == XPROC_C_STATUS_AGAIN; ++i) {
    observer_status = xproc_c_observer_peek_copy(observer, &observed, sizeof(observed), &observed_len);
  }
  ASSERT_EQ(observer_status, XPROC_C_STATUS_OK);
  EXPECT_EQ(observed_len, sizeof(observed));
  EXPECT_EQ(observed, expected);

  std::uint32_t consumed = 0;
  std::uint32_t consumed_len = 0;
  ASSERT_EQ(xproc_c_consumer_poll_copy(consumer, &consumed, sizeof(consumed), &consumed_len), XPROC_C_STATUS_OK);
  EXPECT_EQ(consumed_len, sizeof(consumed));
  EXPECT_EQ(consumed, expected);

  xproc_c_observer_close(observer);
  xproc_c_consumer_close(consumer);
  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

TEST(CApiSmoke, ReadExistingShmOptionsInfersManifestBackedFields) {
  const std::string path = "/xproc_capi_read_existing_options";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options creator{};
  xproc_c_options_init(&creator);
  creator.path = path.c_str();
  creator.shm_size = xproc_c_shm_size_for_data_capacity(8192);
  creator.channel_type = XPROC_C_CHANNEL_FIXED;
  creator.item_size = sizeof(std::uint32_t);
  creator.schema_id = 0x1234ull;
  creator.creator_timestamp_ns = 0x1122334455667788ull;
  creator.creator_flags = 0x8877665544332211ull;

  xproc_c_producer* producer = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&creator, &producer), XPROC_C_STATUS_OK);

  xproc_c_options inferred{};
  ASSERT_EQ(xproc_c_shm_read_existing_options(path.c_str(), "Local", &inferred), XPROC_C_STATUS_OK);
  EXPECT_EQ(inferred.channel_type, XPROC_C_CHANNEL_FIXED);
  EXPECT_EQ(inferred.item_size, sizeof(std::uint32_t));
  EXPECT_EQ(inferred.schema_id, 0x1234ull);
  EXPECT_EQ(inferred.creator_timestamp_ns, 0x1122334455667788ull);
  EXPECT_EQ(inferred.creator_flags, 0x8877665544332211ull);
  EXPECT_EQ(inferred.create_if_missing, 0);

  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

TEST(CApiSmoke, ValidationAndErrorCopy) {
  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = "/xproc_capi_invalid";
  opts.shm_size = xproc_c_shm_size_for_data_capacity(4096);
  opts.channel_type = XPROC_C_CHANNEL_FIXED;

  ASSERT_EQ(xproc_c_validate_options_for(XPROC_C_ENDPOINT_PRODUCER, &opts), XPROC_C_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(xproc_c_status_string(XPROC_C_STATUS_INVALID_ARGUMENT), "invalid_argument");
  EXPECT_STREQ(xproc_c_layout_error_string(xproc_c_last_layout_error()), "none");
  EXPECT_GT(xproc_c_current_process_id(), 0);
  ASSERT_NE(xproc_c_version_string(), nullptr);
  EXPECT_NE(std::strlen(xproc_c_version_string()), 0u);

  std::uint32_t out_len = 0;
  char tiny[4]{};
  EXPECT_EQ(xproc_c_last_error_copy(tiny, sizeof(tiny), &out_len), XPROC_C_STATUS_BUFFER_TOO_SMALL);
  EXPECT_GT(out_len, sizeof(tiny));

  std::vector<char> message(out_len);
  ASSERT_EQ(xproc_c_last_error_copy(message.data(), out_len, &out_len), XPROC_C_STATUS_OK);
  const std::string error_text(message.begin(), message.end());
  EXPECT_NE(error_text.find("fixed channel requires non-zero item_size"), std::string::npos);
}

TEST(CApiSmoke, SharedMemorySizeHelpersRoundTrip) {
  constexpr std::size_t data_capacity = 4096;
  const std::size_t shm_size = xproc_c_shm_size_for_data_capacity(data_capacity);
  EXPECT_EQ(xproc_c_shm_data_capacity_for_size(shm_size), data_capacity);
  EXPECT_EQ(xproc_c_shm_data_capacity_for_size(XPROC_C_INFER_EXISTING_SHM_SIZE), 0u);
}

TEST(CApiSmoke, ObserverRejectsSocketAndReportsLayoutError) {
  xproc_c_options socket_opts;
  xproc_c_options_init(&socket_opts);
  socket_opts.backend = XPROC_C_BACKEND_SOCKET;
  socket_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  socket_opts.item_size = sizeof(std::uint32_t);
  socket_opts.socket_listen = 1;

  ASSERT_EQ(xproc_c_validate_options_for(XPROC_C_ENDPOINT_OBSERVER, &socket_opts), XPROC_C_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(xproc_c_last_error_message()).find("shared_memory"), std::string::npos);

  const std::string path = "/xproc_capi_layout_error";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.path = path.c_str();
  producer_opts.shm_size = xproc_c_shm_size_for_data_capacity(4096);
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);
  producer_opts.schema_id = 0x1234;

  xproc_c_producer* producer = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&producer_opts, &producer), XPROC_C_STATUS_OK);

  xproc_c_options observer_opts;
  xproc_c_options_init(&observer_opts);
  observer_opts.path = path.c_str();
  observer_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  observer_opts.create_if_missing = 0;
  observer_opts.channel_type = XPROC_C_CHANNEL_VARLEN;

  xproc_c_observer* observer = nullptr;
  EXPECT_EQ(xproc_c_observer_open(&observer_opts, &observer), XPROC_C_STATUS_LAYOUT_ERROR);
  EXPECT_EQ(observer, nullptr);
  EXPECT_EQ(xproc_c_last_layout_error(), XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH);
  EXPECT_STREQ(xproc_c_layout_error_string(xproc_c_last_layout_error()), "layout_type_mismatch");

  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

TEST(CApiSmoke, FixedItemSizeAndSchemaIdMismatchReportLayoutErrors) {
  const std::string path = "/xproc_capi_manifest_mismatch";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.path = path.c_str();
  producer_opts.shm_size = xproc_c_shm_size_for_data_capacity(4096);
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);
  producer_opts.schema_id = 0x1122334455667788ull;

  xproc_c_producer* producer = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&producer_opts, &producer), XPROC_C_STATUS_OK);

  xproc_c_options bad_item_opts = producer_opts;
  bad_item_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  bad_item_opts.create_if_missing = 0;
  bad_item_opts.item_size = sizeof(std::uint64_t);

  xproc_c_consumer* bad_item_consumer = nullptr;
  EXPECT_EQ(xproc_c_consumer_open(&bad_item_opts, &bad_item_consumer), XPROC_C_STATUS_LAYOUT_ERROR);
  EXPECT_EQ(bad_item_consumer, nullptr);
  EXPECT_EQ(xproc_c_last_layout_error(), XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH);
  EXPECT_STREQ(xproc_c_layout_error_string(xproc_c_last_layout_error()), "fixed_item_size_mismatch");

  xproc_c_options bad_schema_opts = producer_opts;
  bad_schema_opts.shm_size = XPROC_C_INFER_EXISTING_SHM_SIZE;
  bad_schema_opts.create_if_missing = 0;
  bad_schema_opts.schema_id = producer_opts.schema_id + 1;

  xproc_c_observer* bad_schema_observer = nullptr;
  EXPECT_EQ(xproc_c_observer_open(&bad_schema_opts, &bad_schema_observer), XPROC_C_STATUS_LAYOUT_ERROR);
  EXPECT_EQ(bad_schema_observer, nullptr);
  EXPECT_EQ(xproc_c_last_layout_error(), XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH);
  EXPECT_STREQ(xproc_c_layout_error_string(xproc_c_last_layout_error()), "schema_id_mismatch");

  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

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

TEST(CApiSmoke, SocketRoundTripAndBorrowedOptions) {
  xproc_c_options consumer_opts;
  xproc_c_options_init(&consumer_opts);
  consumer_opts.backend = XPROC_C_BACKEND_SOCKET;
  consumer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  consumer_opts.item_size = sizeof(std::uint32_t);
  consumer_opts.socket_listen = 1;
  consumer_opts.socket_port = 0;

  xproc_c_consumer* consumer = nullptr;
  const xproc_c_status consumer_open_status = xproc_c_consumer_open(&consumer_opts, &consumer);
  if (consumer_open_status != XPROC_C_STATUS_OK) {
    GTEST_SKIP() << "socket transport unavailable in this environment: " << xproc_c_last_error_message();
  }

  xproc_c_options borrowed_consumer{};
  ASSERT_EQ(xproc_c_consumer_options(consumer, &borrowed_consumer), XPROC_C_STATUS_OK);
  EXPECT_EQ(borrowed_consumer.backend, XPROC_C_BACKEND_SOCKET);
  EXPECT_EQ(borrowed_consumer.socket_listen, 1);
  ASSERT_NE(borrowed_consumer.socket_port, 0);

  xproc_c_options producer_opts;
  xproc_c_options_init(&producer_opts);
  producer_opts.backend = XPROC_C_BACKEND_SOCKET;
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);
  producer_opts.socket_host = "127.0.0.1";
  producer_opts.socket_port = borrowed_consumer.socket_port;
  producer_opts.socket_listen = 0;

  xproc_c_producer* producer = nullptr;
  const xproc_c_status producer_open_status = xproc_c_producer_open(&producer_opts, &producer);
  if (producer_open_status != XPROC_C_STATUS_OK) {
    xproc_c_consumer_close(consumer);
    GTEST_SKIP() << "socket producer unavailable in this environment: " << xproc_c_last_error_message();
  }

  xproc_c_options borrowed_producer{};
  ASSERT_EQ(xproc_c_producer_options(producer, &borrowed_producer), XPROC_C_STATUS_OK);
  EXPECT_EQ(borrowed_producer.socket_port, borrowed_consumer.socket_port);

  const std::uint32_t expected = 0xCAFEBABEu;
  ASSERT_EQ(xproc_c_producer_send_fixed_sized(producer, &expected, sizeof(expected)), XPROC_C_STATUS_OK);

  std::uint32_t actual = 0;
  std::uint32_t out_len = 0;
  bool received = false;
  for (int i = 0; i < 200; ++i) {
    const xproc_c_status status = xproc_c_consumer_poll_copy(consumer, &actual, sizeof(actual), &out_len);
    if (status == XPROC_C_STATUS_OK) {
      received = true;
      break;
    }
    ASSERT_EQ(status, XPROC_C_STATUS_AGAIN);
    ASSERT_EQ(xproc_c_consumer_wait(consumer), XPROC_C_STATUS_OK);
  }

  ASSERT_TRUE(received);
  EXPECT_EQ(out_len, sizeof(actual));
  EXPECT_EQ(actual, expected);

  xproc_c_producer_close(producer);
  xproc_c_consumer_close(consumer);
}
