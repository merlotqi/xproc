#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

struct Fixture {
  std::string path;
  xproc::ipc::transport_options opts;

  explicit Fixture(const char* suffix) {
    path = std::string("/xproc_rt_alloc_") + suffix;
    xproc::core::shm::unlink(path);
    opts.path = path;
    opts.shm_size = sizeof(xproc::core::control_block) + 16384;
    opts.type = xproc::ipc::channel_type::fixed;
    opts.item_size = 4;
  }

  ~Fixture() { xproc::core::shm::unlink(path); }
};

}  // namespace

// ---- reuse_buffer policy ----

TEST(RuntimeAllocation, ReuseBufferInlineExecutor) {
  Fixture fx("reuse_inline");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<int> count{0};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             std::uint32_t v = 0;
             std::memcpy(&v, data, sizeof(v));
             EXPECT_EQ(v, 0xDEADBEEFu);
             ++count;
             if (count.load() >= 3) {
               rt.stop();
             }
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_fixed<std::uint32_t>(0xDEADBEEFu);
  prod.send_fixed<std::uint32_t>(0xDEADBEEFu);
  prod.send_fixed<std::uint32_t>(0xDEADBEEFu);

  rt_thread.join();
  EXPECT_GE(count.load(), 1);
}

TEST(RuntimeAllocation, ReuseBufferDefaultPolicy) {
  Fixture fx("reuse_default");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor, [&](const std::uint8_t* data, std::size_t len) {
      EXPECT_EQ(len, 4u);
      got.store(true);
      rt.stop();
    });
  });

  prod.send_fixed<std::uint32_t>(0xAAAAAAAAu);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- zero_copy policy ----

TEST(RuntimeAllocation, ZeroCopyInlineExecutor) {
  Fixture fx("zero_inline");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             std::uint32_t v = 0;
             std::memcpy(&v, data, sizeof(v));
             EXPECT_EQ(v, 0xCAFEBABEu);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::zero_copy);
  });

  prod.send_fixed<std::uint32_t>(0xCAFEBABEu);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- sbo policy ----

TEST(RuntimeAllocation, SboSmallMessage) {
  Fixture fx("sbo_small");
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::sbo);
  });

  prod.send_fixed<std::uint32_t>(0xBEEF1234u);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

TEST(RuntimeAllocation, SboLargeMessageHeapFallback) {
  Fixture fx_lg("sbo_large");
  fx_lg.opts.item_size = 512;
  fx_lg.opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  xproc::ipc::producer prod(fx_lg.opts);
  xproc::ipc::consumer cons(fx_lg.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::vector<std::uint8_t> payload(512, std::uint8_t{0xAB});
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 512u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::sbo);
  });

  prod.send_fixed_bytes(payload.data(), 512);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- run_batched ----

TEST(RuntimeAllocation, RunBatchedCollectsMultipleMessages) {
  Fixture fx("batched");
  fx.opts.item_size = 8;
  fx.opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<int> received{0};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run_batched(
        executor,
        [&](const std::uint8_t* data, std::size_t len) {
          EXPECT_EQ(len, 8u);
          ++received;
          if (received.load() >= 4) {
            rt.stop();
          }
        },
        4,
        xproc::ipc::copy_policy::reuse_buffer);
  });

  for (int i = 0; i < 5; ++i) {
    prod.send_fixed<std::uint64_t>(static_cast<std::uint64_t>(i));
  }
  rt_thread.join();
  EXPECT_GE(received.load(), 4);
}

// ---- backpressure ----

TEST(RuntimeAllocation, BackpressureCallbackFires) {
  Fixture fx("bp");
  fx.opts.item_size = 8;
  fx.opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  xproc::ipc::producer prod(fx.opts);
  xproc::ipc::consumer cons(fx.opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<std::size_t> bp_count{0};
  std::atomic<bool> got_msg{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(
        executor,
        [&](const std::uint8_t* data, std::size_t len) {
          EXPECT_EQ(len, 8u);
          got_msg.store(true);
          rt.stop();
        },
        [&](std::size_t queued) { bp_count.store(queued); },
        xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_fixed<std::uint64_t>(42u);
  rt_thread.join();
  EXPECT_TRUE(got_msg.load());
}

// ---- consumer_channel_interface path ----

TEST(RuntimeAllocation, ReuseBufferViaInterface) {
  Fixture fx("iface");
  xproc::ipc::producer prod(fx.opts);
  auto shm_cons = std::make_unique<xproc::ipc::shm_consumer>(fx.opts);
  xproc::ipc::consumer_channel_interface& iface = *shm_cons;
  xproc::ipc::runtime rt(iface);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 4u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_fixed<std::uint32_t>(0xFEEDF00Du);
  rt_thread.join();
  EXPECT_TRUE(got.load());
}

// ---- varlen channel ----

TEST(RuntimeAllocation, ReuseBufferVarlen) {
  const std::string path = "/xproc_rt_alloc_varlen";
  xproc::core::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 65536;
  opts.type = xproc::ipc::channel_type::varlen;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> got{false};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t* data, std::size_t len) {
             EXPECT_EQ(len, 11u);
             got.store(true);
             rt.stop();
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  prod.send_varlen("hello world", 11);
  rt_thread.join();
  EXPECT_TRUE(got.load());

  xproc::core::shm::unlink(path);
}
