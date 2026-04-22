#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

TEST(ApiSurface, PlatformInfoAndProcessId) {
  EXPECT_NE(xproc::platform::platform_info::os, nullptr);
  EXPECT_NE(xproc::platform::arch_info::name, nullptr);
  EXPECT_GT(xproc::platform::current_process_id(), 0);
  EXPECT_NE(xproc::platform::platform_info::is_linux(), xproc::platform::platform_info::is_windows());
}

TEST(ApiSurface, AtomicWaitNotifyOne) {
  alignas(8) std::atomic<std::uint32_t> word{0u};
  std::atomic<bool> progressed{false};
  std::thread waiter([&] {
    xproc::sync::atomic_wait(&word, 0u);
    progressed.store(true, std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  word.store(1u, std::memory_order_release);
  xproc::sync::atomic_notify_one(&word);
  waiter.join();
  EXPECT_TRUE(progressed.load(std::memory_order_acquire));
}

TEST(ApiSurface, AtomicWaitNotifyAll) {
  alignas(8) std::atomic<std::uint32_t> word{0u};
  std::atomic<int> progressed{0};
  std::thread w1([&] {
    xproc::sync::atomic_wait(&word, 0u);
    progressed.fetch_add(1, std::memory_order_release);
  });
  std::thread w2([&] {
    xproc::sync::atomic_wait(&word, 0u);
    progressed.fetch_add(1, std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  word.store(1u, std::memory_order_release);
  xproc::sync::atomic_notify_all(&word);
  w1.join();
  w2.join();
  EXPECT_EQ(progressed.load(std::memory_order_acquire), 2);
}

TEST(ApiSurface, AtomicBackoffPauseAndReset) {
  std::atomic<std::uint32_t> v{0u};
  xproc::sync::atomic_backoff backoff(/*spin_threshold=*/0);
  backoff.pause(v, 1u);
  backoff.reset();
  SUCCEED();
}

TEST(ApiSurface, ShmOpenModeCreateOpenReadAndErrors) {
  const std::string path = "/xproc_api_surface_shm_modes";
  const std::string create_path = "/xproc_api_surface_shm_modes_open_create";
  const std::size_t shm_bytes = sizeof(xproc::shm::control_block) + 4096;
  xproc::shm::shm::unlink(path);
  xproc::shm::shm::unlink(create_path);

  xproc::shm::shm creator;
  ASSERT_TRUE(creator.open(path, shm_bytes, xproc::shm::shm_open_mode::create));
  EXPECT_TRUE(creator.created_this_open());
  // POSIX shm survives until unlink after all fds are closed; Windows named mappings are deleted
  // when the last handle closes - keep creator mapped until other modes have opened.

  xproc::shm::shm opener;
  ASSERT_TRUE(opener.open(path, shm_bytes, xproc::shm::shm_open_mode::open));
  EXPECT_FALSE(opener.created_this_open());
  opener.detach();

  xproc::shm::shm reader;
  ASSERT_TRUE(reader.open(path, shm_bytes, xproc::shm::shm_open_mode::read));
  EXPECT_FALSE(reader.created_this_open());
  reader.detach();

  xproc::shm::shm open_create;
  ASSERT_TRUE(open_create.open(path, shm_bytes, xproc::shm::shm_open_mode::open_create));
  EXPECT_FALSE(open_create.created_this_open());
  open_create.detach();

  xproc::shm::shm open_create_missing;
  ASSERT_TRUE(open_create_missing.open(create_path, shm_bytes, xproc::shm::shm_open_mode::open_create));
  EXPECT_TRUE(open_create_missing.created_this_open());
  open_create_missing.detach();

  creator.detach();

  xproc::shm::shm missing;
  EXPECT_FALSE(missing.open("/xproc_api_surface_missing", 4096, xproc::shm::shm_open_mode::open));
  EXPECT_NE(missing.last_os_error(), 0);

  xproc::shm::shm::unlink(path);
  xproc::shm::shm::unlink(create_path);
}

#if defined(_WIN32)
TEST(ApiSurface, ShmOpenRejectsInvalidWin32Namespace) {
  xproc::shm::shm sm;
  EXPECT_FALSE(sm.open("/xproc_api_bad_ns", 4096, xproc::shm::shm_open_mode::create, "Session"));
  EXPECT_NE(sm.last_os_error(), 0);
}

TEST(ApiSurface, TransportOptionsRejectsBadWin32Namespace) {
  xproc::ipc::transport_options opts;
  opts.path = "/xproc_api_bad_transport_ns";
  opts.shm_size = sizeof(xproc::shm::control_block) + 4096;
  opts.item_size = 4;
  opts.win32_object_namespace = "Bad";
  EXPECT_THROW(xproc::ipc::validate_transport_options(opts), std::invalid_argument);
}
#endif

TEST(ApiSurface, IpcRuntimeRunAndStop) {
  const std::string path = "/xproc_api_surface_runtime";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 16384;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 4;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  xproc::ipc::runtime runtime(consumer);

  std::atomic<bool> got{false};
  std::thread rt([&] {
    auto executor = [](auto task) { task(); };
    runtime.run(executor, [&](const std::uint8_t* data, std::size_t len) {
      EXPECT_EQ(len, 4u);
      std::uint32_t v = 0;
      std::memcpy(&v, data, sizeof(v));
      EXPECT_EQ(v, 0x12345678u);
      got.store(true, std::memory_order_release);
      runtime.stop();
    });
  });

  producer.send_fixed<std::uint32_t>(0x12345678u);

  for (int i = 0; i < 100 && !got.load(std::memory_order_acquire); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_TRUE(got.load(std::memory_order_acquire));
  runtime.stop();
  rt.join();

  xproc::shm::shm::unlink(path);
}

TEST(ApiSurface, IpcInspectorPolymorphism) {
  const std::string path = "/xproc_api_surface_inspector";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::control_block) + 8192;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);

  xproc::ipc::producer producer(opts);
  xproc::ipc::observer observer(opts);
  producer.send_fixed<std::uint32_t>(1u);

  const xproc::ipc::ring_inspector_interface& insp = observer;
  const xproc::ipc::attach_count_view_interface& attach = observer;
  const auto snap = insp.snapshot();
  EXPECT_GE(snap.attach_count, attach.attach_count());
  EXPECT_GE(snap.commit_seq, 1u);

  xproc::shm::shm::unlink(path);
}
