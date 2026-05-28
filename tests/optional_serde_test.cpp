#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

#if defined(XPROC_WITH_PROTOBUF)
#include "test_point.pb.h"
#endif

namespace {

#if defined(XPROC_WITH_NLOHMANN_JSON)
TEST(OptionalSerde, NlohmannJsonRoundtrip) {
  const std::string path = "/xproc_optional_json_test";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::varlen;

  std::atomic<bool> producer_ready{false};
  nlohmann::json received;
  std::atomic<bool> got_msg{false};

  std::thread consumer([&] {
    while (!producer_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
    while (!got_msg.load(std::memory_order_acquire)) {
      if (xproc::ipc::poll_decoded<xproc::protocol::nlohmann_json_codec<4096>>(ch, [&](const nlohmann::json& m) {
            received = m;
            got_msg.store(true, std::memory_order_release);
          })) {
        continue;
      }
      std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
    }
  });

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    producer_ready.store(true, std::memory_order_release);
    nlohmann::json msg;
    msg["x"] = 42;
    msg["y"] = "ipc";
    xproc::ipc::send_encoded<xproc::protocol::nlohmann_json_codec<4096>>(prod, msg);
  }

  consumer.join();
  EXPECT_EQ(received["x"].get<int>(), 42);
  EXPECT_EQ(received["y"].get<std::string>(), "ipc");

  xproc::core::shm::unlink(path);
}
#endif

#if defined(XPROC_WITH_PROTOBUF)
TEST(OptionalSerde, ProtobufRoundtrip) {
  const std::string path = "/xproc_optional_proto_test";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::varlen;

  std::atomic<bool> producer_ready{false};
  xproc::test::TestPoint received;
  std::atomic<bool> got_msg{false};

  using codec = xproc::protocol::protobuf_message_codec<xproc::test::TestPoint, 256>;

  std::thread consumer([&] {
    while (!producer_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
    while (!got_msg.load(std::memory_order_acquire)) {
      if (xproc::ipc::poll_decoded<codec>(ch, [&](const xproc::test::TestPoint& m) {
            received = m;
            got_msg.store(true, std::memory_order_release);
          })) {
        continue;
      }
      std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
    }
  });

  {
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    producer_ready.store(true, std::memory_order_release);
    xproc::test::TestPoint msg;
    msg.set_x(7);
    msg.set_y(-3);
    xproc::ipc::send_encoded<codec>(prod, msg);
  }

  consumer.join();
  EXPECT_EQ(received.x(), 7);
  EXPECT_EQ(received.y(), -3);

  xproc::core::shm::unlink(path);
}
#endif

#if !defined(XPROC_WITH_NLOHMANN_JSON) && !defined(XPROC_WITH_PROTOBUF)
TEST(OptionalSerde, DisabledByBuildOptions) {
  GTEST_SKIP() << "Enable XPROC_WITH_NLOHMANN_JSON or XPROC_WITH_PROTOBUF to run optional serde tests.";
}
#endif

}  // namespace
