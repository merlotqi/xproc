#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <xproc/sync/atomic_wait.hpp>
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

  void request_exit_message() noexcept {
    emit_exit_message_.store(true, std::memory_order_release);
    interrupt_wait();
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
  bool poll_impl(const std::function<void(void*, std::uint32_t)>& handler) override {
    if (emit_exit_message_.exchange(false, std::memory_order_acq_rel)) {
      handler(&dummy_, 0);
      return true;
    }
    return false;
  }

 private:
  xproc::ipc::transport_options opts_{};
  std::mutex mu_;
  std::condition_variable cv_;
  bool interrupted_{false};
  std::atomic<bool> entered_wait_{false};
  std::atomic<bool> emit_exit_message_{false};
  std::uint8_t dummy_{0};
};

bool wait_for_true(const std::atomic<bool>& flag, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (flag.load(std::memory_order_acquire)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return flag.load(std::memory_order_acquire);
}

bool stop_fake_runtime_until_returned(xproc::ipc::runtime& rt, BlockingInterfaceConsumer& cons,
                                      const std::atomic<bool>& returned, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    rt.stop();
    cons.request_exit_message();
    if (returned.load(std::memory_order_acquire)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  rt.stop();
  cons.request_exit_message();
  return returned.load(std::memory_order_acquire);
}

class SharedHeaderInterfaceConsumer final : public xproc::ipc::consumer_channel_interface {
 public:
  const xproc::ipc::transport_options& options() const noexcept override { return opts_; }

  xproc::core::control_block* shared_header() noexcept override { return &header_; }
  const xproc::core::control_block* shared_header() const noexcept override { return &header_; }

  void wait() override {
    entered_wait_.store(true, std::memory_order_release);
    const auto last_commit = header_.rb_meta.commit_seq.load(std::memory_order_acquire);
    xproc::sync::atomic_wait(&header_.rb_meta.commit_seq, last_commit);
  }

  bool wait_entered_for(std::chrono::milliseconds timeout) const { return wait_for_true(entered_wait_, timeout); }

  void pause_before_wait_snapshot() {
    pause_before_wait_snapshot_.store(true, std::memory_order_release);
    entered_wait_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(gate_mu_);
    wait_snapshot_gate_open_ = false;
  }

  void release_wait_snapshot_gate() {
    std::lock_guard<std::mutex> lock(gate_mu_);
    wait_snapshot_gate_open_ = true;
    gate_cv_.notify_all();
  }

  void force_wake() noexcept {
    header_.rb_meta.commit_seq.fetch_add(1, std::memory_order_release);
    xproc::sync::atomic_notify_all(&header_.rb_meta.commit_seq);
  }

  void request_exit_message() noexcept {
    emit_exit_message_.store(true, std::memory_order_release);
    release_wait_snapshot_gate();
    force_wake();
  }

 protected:
  bool poll_impl(const std::function<void(void*, std::uint32_t)>& handler) override {
    if (emit_exit_message_.exchange(false, std::memory_order_acq_rel)) {
      handler(&dummy_, 0);
      return true;
    }
    entered_wait_.store(true, std::memory_order_release);
    if (pause_before_wait_snapshot_.exchange(false, std::memory_order_acq_rel)) {
      std::unique_lock<std::mutex> lock(gate_mu_);
      gate_cv_.wait(lock, [&] { return wait_snapshot_gate_open_; });
    }
    return false;
  }

 private:
  xproc::ipc::transport_options opts_{};
  xproc::core::control_block header_{};
  std::atomic<bool> entered_wait_{false};
  std::atomic<bool> emit_exit_message_{false};
  std::atomic<bool> pause_before_wait_snapshot_{false};
  std::mutex gate_mu_;
  std::condition_variable gate_cv_;
  bool wait_snapshot_gate_open_{false};
  std::uint8_t dummy_{0};
};

bool stop_shared_header_runtime_until_returned(xproc::ipc::runtime& rt, SharedHeaderInterfaceConsumer& cons,
                                               const std::atomic<bool>& returned,
                                               std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    rt.stop();
    cons.request_exit_message();
    if (returned.load(std::memory_order_acquire)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  rt.stop();
  cons.request_exit_message();
  return returned.load(std::memory_order_acquire);
}

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
    rt.run(
        executor,
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
    rt.run(
        executor,
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
    rt.run(
        executor,
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
    rt.run(
        executor,
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
        4, xproc::ipc::copy_policy::reuse_buffer);
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
        [&](std::size_t queued) { bp_count.store(queued); }, xproc::ipc::copy_policy::reuse_buffer);
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
    rt.run(
        executor,
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

TEST(RuntimeAllocation, StopInterruptsInterfaceWait) {
  auto cons = std::make_shared<BlockingInterfaceConsumer>();
  auto rt = std::make_shared<xproc::ipc::runtime>(*cons);
  auto returned = std::make_shared<std::atomic<bool>>(false);

  std::thread rt_thread([rt, cons, returned] {
    auto executor = [](auto task) { task(); };
    rt->run(executor, [rt](const std::uint8_t*, std::size_t) { rt->stop(); });
    returned->store(true, std::memory_order_release);
  });

  const bool entered_wait = cons->wait_entered_for(std::chrono::milliseconds(250));
  EXPECT_TRUE(entered_wait);
  if (!entered_wait) {
    const bool cleaned = stop_fake_runtime_until_returned(*rt, *cons, *returned, std::chrono::milliseconds(250));
    EXPECT_TRUE(cleaned);
    rt_thread.join();
    return;
  }

  rt->stop();
  const bool returned_after_stop = wait_for_true(*returned, std::chrono::milliseconds(250));
  bool cleaned = true;
  if (!returned_after_stop) {
    cleaned = stop_fake_runtime_until_returned(*rt, *cons, *returned, std::chrono::milliseconds(250));
  }
  EXPECT_TRUE(cleaned);
  rt_thread.join();
  EXPECT_TRUE(returned_after_stop);
}

TEST(RuntimeAllocation, StopInterruptsSharedHeaderInterfaceWait) {
  auto cons = std::make_shared<SharedHeaderInterfaceConsumer>();
  auto rt = std::make_shared<xproc::ipc::runtime>(*cons);
  auto returned = std::make_shared<std::atomic<bool>>(false);

  std::thread rt_thread([rt, cons, returned] {
    auto executor = [](auto task) { task(); };
    rt->run(executor, [rt](const std::uint8_t*, std::size_t) { rt->stop(); });
    returned->store(true, std::memory_order_release);
  });

  const bool entered_wait = cons->wait_entered_for(std::chrono::milliseconds(250));
  EXPECT_TRUE(entered_wait);
  if (!entered_wait) {
    const bool cleaned = stop_shared_header_runtime_until_returned(*rt, *cons, *returned, std::chrono::milliseconds(250));
    EXPECT_TRUE(cleaned);
    rt_thread.join();
    return;
  }

  rt->stop();
  const bool returned_after_stop = wait_for_true(*returned, std::chrono::milliseconds(250));
  bool cleaned = true;
  if (!returned_after_stop) {
    cleaned = stop_shared_header_runtime_until_returned(*rt, *cons, *returned, std::chrono::milliseconds(250));
  }
  EXPECT_TRUE(cleaned);
  rt_thread.join();
  EXPECT_TRUE(returned_after_stop);
}

TEST(RuntimeAllocation, StopBeforeSharedHeaderWaitSnapshotDoesNotHang) {
  auto cons = std::make_shared<SharedHeaderInterfaceConsumer>();
  auto rt = std::make_shared<xproc::ipc::runtime>(*cons);
  auto returned = std::make_shared<std::atomic<bool>>(false);
  cons->pause_before_wait_snapshot();

  std::thread rt_thread([rt, cons, returned] {
    auto executor = [](auto task) { task(); };
    rt->run(executor, [rt](const std::uint8_t*, std::size_t) { rt->stop(); });
    returned->store(true, std::memory_order_release);
  });

  const bool entered_wait = cons->wait_entered_for(std::chrono::milliseconds(250));
  EXPECT_TRUE(entered_wait);
  if (!entered_wait) {
    const bool cleaned = stop_shared_header_runtime_until_returned(*rt, *cons, *returned, std::chrono::milliseconds(250));
    EXPECT_TRUE(cleaned);
    rt_thread.join();
    return;
  }

  rt->stop();
  cons->release_wait_snapshot_gate();
  const bool returned_after_stop = wait_for_true(*returned, std::chrono::milliseconds(250));
  bool cleaned = true;
  if (!returned_after_stop) {
    cleaned = stop_shared_header_runtime_until_returned(*rt, *cons, *returned, std::chrono::milliseconds(250));
  }
  EXPECT_TRUE(cleaned);
  rt_thread.join();
  EXPECT_TRUE(returned_after_stop);
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
    rt.run(
        executor,
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
