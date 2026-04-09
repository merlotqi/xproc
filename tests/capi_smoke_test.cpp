#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <xproc_c.h>
}

#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout.hpp>

TEST(CApiSmoke, FixedProducerConsumerRoundTrip) {
  const std::string path = "/xproc_capi_fixed_roundtrip";
  ASSERT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path.c_str();
  opts.shm_size = sizeof(xproc::shm::control_block) + 8192;
  opts.channel_type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(std::uint32_t);

  xproc_c_producer* producer = nullptr;
  xproc_c_consumer* consumer = nullptr;

  ASSERT_EQ(xproc_c_producer_open(&opts, &producer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_consumer_open(&opts, &consumer), XPROC_C_STATUS_OK);

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

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path.c_str();
  opts.shm_size = sizeof(xproc::shm::control_block) + 8192;
  opts.channel_type = XPROC_C_CHANNEL_VARLEN;

  xproc_c_producer* producer = nullptr;
  xproc_c_consumer* consumer = nullptr;

  ASSERT_EQ(xproc_c_producer_open(&opts, &producer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_consumer_open(&opts, &consumer), XPROC_C_STATUS_OK);

  const std::array<std::uint8_t, 6> expected{{1u, 2u, 3u, 4u, 5u, 6u}};
  ASSERT_EQ(xproc_c_producer_send_varlen(producer, expected.data(), static_cast<std::uint32_t>(expected.size())),
            XPROC_C_STATUS_OK);

  std::array<std::uint8_t, 2> too_small{};
  std::uint32_t out_len = 0;
  EXPECT_EQ(xproc_c_consumer_poll_copy(consumer, too_small.data(), static_cast<std::uint32_t>(too_small.size()),
                                       &out_len),
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

  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = path.c_str();
  opts.shm_size = sizeof(xproc::shm::control_block) + 8192;
  opts.channel_type = XPROC_C_CHANNEL_FIXED;
  opts.item_size = sizeof(std::uint32_t);

  xproc_c_producer* producer = nullptr;
  xproc_c_observer* observer = nullptr;

  ASSERT_EQ(xproc_c_producer_open(&opts, &producer), XPROC_C_STATUS_OK);
  ASSERT_EQ(xproc_c_observer_open(&opts, &observer), XPROC_C_STATUS_OK);

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

  std::uint32_t actual = 0;
  std::uint32_t out_len = 0;
  ASSERT_EQ(xproc_c_observer_peek_copy(observer, &actual, sizeof(actual), &out_len), XPROC_C_STATUS_OK);
  EXPECT_EQ(out_len, sizeof(actual));
  EXPECT_EQ(actual, expected);

  xproc_c_observer_close(observer);
  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
}

TEST(CApiSmoke, ValidationAndErrorCopy) {
  xproc_c_options opts;
  xproc_c_options_init(&opts);
  opts.path = "/xproc_capi_invalid";
  opts.shm_size = sizeof(xproc::shm::control_block) + 4096;
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
  producer_opts.shm_size = sizeof(xproc::shm::control_block) + 4096;
  producer_opts.channel_type = XPROC_C_CHANNEL_FIXED;
  producer_opts.item_size = sizeof(std::uint32_t);

  xproc_c_producer* producer = nullptr;
  ASSERT_EQ(xproc_c_producer_open(&producer_opts, &producer), XPROC_C_STATUS_OK);

  xproc_c_options observer_opts;
  xproc_c_options_init(&observer_opts);
  observer_opts.path = path.c_str();
  observer_opts.shm_size = producer_opts.shm_size;
  observer_opts.channel_type = XPROC_C_CHANNEL_VARLEN;

  xproc_c_observer* observer = nullptr;
  EXPECT_EQ(xproc_c_observer_open(&observer_opts, &observer), XPROC_C_STATUS_LAYOUT_ERROR);
  EXPECT_EQ(observer, nullptr);
  EXPECT_EQ(xproc_c_last_layout_error(), XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH);
  EXPECT_STREQ(xproc_c_layout_error_string(xproc_c_last_layout_error()), "layout_type_mismatch");

  xproc_c_producer_close(producer);
  EXPECT_EQ(xproc_c_shm_unlink(path.c_str()), XPROC_C_STATUS_OK);
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
