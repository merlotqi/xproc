// optional_serde_test.cpp previously tested send_encoded / poll_decoded with optional
// third-party codecs (nlohmann::json, protobuf).
//
// xproc is now a pure buffer transport layer — codec abstractions were removed.
// Users serialise/deserialise in their own application code.
//
// This file demonstrates the recommended pattern:
//   1. Serialise struct → bytes in user code.
//   2. Send via ch.send_varlen / ch.send_fixed_bytes.
//   3. Receive via ch.poll and deserialise in the handler.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

// -----------------------------------------------------------------------
//  nlohmann::json roundtrip using raw send/poll — no framework codec.
// -----------------------------------------------------------------------
#if defined(XPROC_WITH_NLOHMANN_JSON)
#include <nlohmann/json.hpp>

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
      if (ch.poll([&](const xproc::ipc::message_meta&, void* p, std::uint32_t len) {
            auto* bytes = static_cast<const std::byte*>(p);
            // User deserialisation: parse bytes → json
            received = nlohmann::json::parse(bytes, bytes + len);
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

    // User serialisation: json → bytes
    nlohmann::json msg;
    msg["x"] = 42;
    msg["y"] = "ipc";
    std::string json_str = msg.dump();
    prod.send_varlen(json_str.data(), static_cast<std::uint32_t>(json_str.size()));
  }

  consumer.join();
  EXPECT_EQ(received["x"].get<int>(), 42);
  EXPECT_EQ(received["y"].get<std::string>(), "ipc");

  xproc::core::shm::unlink(path);
}
#endif

// -----------------------------------------------------------------------
//  protobuf roundtrip using raw send/poll — no framework codec.
// -----------------------------------------------------------------------
#if defined(XPROC_WITH_PROTOBUF)
#include "test_point.pb.h"

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

  std::thread consumer([&] {
    while (!producer_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);
    while (!got_msg.load(std::memory_order_acquire)) {
      if (ch.poll([&](const xproc::ipc::message_meta&, void* p, std::uint32_t len) {
            auto* bytes = static_cast<const std::byte*>(p);
            // User deserialisation: bytes → protobuf
            received.ParseFromArray(bytes, static_cast<int>(len));
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

    // User serialisation: protobuf → bytes
    xproc::test::TestPoint msg;
    msg.set_x(7);
    msg.set_y(-3);
    std::string serialised;
    msg.SerializeToString(&serialised);
    prod.send_varlen(serialised.data(), static_cast<std::uint32_t>(serialised.size()));
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
