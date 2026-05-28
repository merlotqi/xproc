// Linux only: shared memory, fork, and futex cross-process behaviour (see tests/CMakeLists.txt).

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

TEST(IpcIntegration, FixedItemSizeZeroRejected) {
  xproc::ipc::transport_options opts;
  opts.path = "/xproc_test_item_zero";
  opts.shm_size = sizeof(xproc::core::control_block) + 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 0;
  EXPECT_THROW((void)xproc::ipc::channel(opts, xproc::ipc::endpoint::role::producer), std::invalid_argument);
}

TEST(IpcIntegration, ObserverEndpointRejected) {
  xproc::ipc::transport_options opts;
  opts.path = "/xproc_test_observer";
  opts.shm_size = sizeof(xproc::core::control_block) + 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 4;
  bool threw = false;
  try {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    xproc::ipc::endpoint ep(opts, xproc::ipc::endpoint::role::observer);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    (void)ep;
  } catch (const std::logic_error& e) {
    threw = true;
    EXPECT_NE(std::string(e.what()).find("xproc::ipc::observer"), std::string::npos);
  }
  EXPECT_TRUE(threw);
}

TEST(IpcIntegration, ConsumerLayoutErrorIncludesReason) {
  const char* path = "/xproc_test_layout_msg";
  xproc::core::shm::unlink(path);
  xproc::core::shm sm;
  ASSERT_TRUE(sm.open(path, sizeof(xproc::core::control_block) + 512, xproc::core::shm_open_mode::open_create));
  sm.detach();

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 4;
  opts.create_if_missing = false;

  try {
    (void)xproc::ipc::channel(opts, xproc::ipc::endpoint::role::consumer);
    FAIL() << "expected layout_exception";
  } catch (const xproc::core::layout_exception& e) {
    EXPECT_EQ(e.code(), xproc::core::validate_error::bad_magic);
    EXPECT_NE(std::string(e.what()).find("bad magic"), std::string::npos);
  }
  xproc::core::shm::unlink(path);
}

TEST(IpcIntegration, ShmSizeRejected) {
  xproc::ipc::transport_options opts;
  opts.path = "/xproc_test_shm_too_small";
  opts.shm_size = 1;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 4;
  EXPECT_THROW((void)xproc::ipc::channel(opts, xproc::ipc::endpoint::role::producer), std::invalid_argument);
}

TEST(IpcIntegration, ValidateLayoutMismatch) {
  xproc::core::control_block h{};
  using lm = xproc::core::layout_manager;
  h.magic = lm::expected_magic;
  h.version_major = lm::version_major;
  h.version_minor = lm::version_minor;
  h.header_size = sizeof(xproc::core::control_block);
  h.layout_type = 0;
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  EXPECT_FALSE(lm::validate(&h, 100, 1u, 8u));
}

TEST(IpcIntegration, RoleSendPoll) {
  constexpr std::size_t total = sizeof(xproc::core::control_block) + 256;
  xproc::ipc::transport_options o;
  o.path = "/xproc_role_test";
  o.shm_size = total;
  o.type = xproc::ipc::channel_type::fixed;
  o.item_size = 4;
  xproc::core::shm::unlink(o.path);
  xproc::ipc::channel prod(o, xproc::ipc::endpoint::role::producer);
  EXPECT_THROW(prod.poll([](void*, std::uint32_t) {}), std::logic_error);
  xproc::core::shm::unlink(o.path);
}

TEST(IpcIntegration, AttachersCanInferExistingShmSize) {
  const std::string path = "/xproc_attach_infer_size";
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options creator_opts;
  creator_opts.path = path;
  creator_opts.shm_size = xproc::ipc::shm_size_for_data_capacity(8192);
  creator_opts.type = xproc::ipc::channel_type::fixed;
  creator_opts.item_size = sizeof(std::uint32_t);
  creator_opts.create_if_missing = true;

  xproc::ipc::producer producer(creator_opts);

  xproc::ipc::transport_options attach_opts = creator_opts;
  attach_opts.shm_size = xproc::ipc::infer_existing_shm_size;
  attach_opts.create_if_missing = false;

  xproc::ipc::consumer consumer(attach_opts);
  xproc::ipc::observer observer(attach_opts);

  producer.send_fixed<std::uint32_t>(0x2468ace0u);

  bool peeked = false;
  while (!peeked) {
    peeked = observer.peek([&](const void* p, std::uint32_t len) {
      EXPECT_EQ(len, sizeof(std::uint32_t));
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      EXPECT_EQ(v, 0x2468ace0u);
    });
    if (!peeked) {
      const std::uint32_t c = observer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&observer.header()->rb_meta.commit_seq, c);
    }
  }

  bool consumed = false;
  while (!consumed) {
    consumed = consumer.poll([&](void* p, std::uint32_t len) {
      EXPECT_EQ(len, sizeof(std::uint32_t));
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      EXPECT_EQ(v, 0x2468ace0u);
    });
    if (!consumed) {
      const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
    }
  }

  EXPECT_GE(observer.attach_count(), 2u);
  xproc::core::shm::unlink(path);
}

namespace {

int cross_process_futex_block_main(const char* shm_path) {
  pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }

  if (pid == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xproc::ipc::transport_options opts;
    opts.path = shm_path;
    opts.shm_size = sizeof(xproc::core::control_block) + 8192;
    opts.type = xproc::ipc::channel_type::fixed;
    opts.item_size = sizeof(std::uint32_t);
    opts.create_if_missing = false;
    xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);

    xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, 0u);

    std::uint32_t val = 0;
    bool got = false;
    while (!got) {
      got = ch.poll([&](void* p, std::uint32_t len) {
        (void)len;
        std::memcpy(&val, p, sizeof(val));
      });
      if (!got) {
        std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
      }
    }
    if (val != 0xdeadbeefu) {
      _exit(2);
    }
    _exit(0);
  }

  int st = 0;
  {
    xproc::ipc::transport_options opts;
    opts.path = shm_path;
    opts.shm_size = sizeof(xproc::core::control_block) + 8192;
    opts.type = xproc::ipc::channel_type::fixed;
    opts.item_size = sizeof(std::uint32_t);
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    prod.send_fixed(0xdeadbeefu);
    waitpid(pid, &st, 0);
  }
  xproc::core::shm::unlink(shm_path);

  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    return 1;
  }
  return 0;
}

int cross_process_varlen_main(const char* shm_path) {
  pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }

  const char* msg = "hello-varlen-ipc";

  if (pid == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    xproc::ipc::transport_options opts;
    opts.path = shm_path;
    opts.shm_size = sizeof(xproc::core::control_block) + 16384;
    opts.type = xproc::ipc::channel_type::varlen;
    opts.create_if_missing = false;
    xproc::ipc::channel ch(opts, xproc::ipc::endpoint::role::consumer);

    bool got = false;
    while (!got) {
      got = ch.poll([&](void* p, std::uint32_t len) {
        if (len != std::strlen(msg) || std::memcmp(p, msg, len) != 0) {
          _exit(3);
        }
      });
      if (!got) {
        std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
      }
    }
    _exit(0);
  }

  int st = 0;
  {
    xproc::ipc::transport_options opts;
    opts.path = shm_path;
    opts.shm_size = sizeof(xproc::core::control_block) + 16384;
    opts.type = xproc::ipc::channel_type::varlen;
    xproc::ipc::channel prod(opts, xproc::ipc::endpoint::role::producer);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    prod.send_varlen(msg, static_cast<std::uint32_t>(std::strlen(msg)));
    waitpid(pid, &st, 0);
  }
  xproc::core::shm::unlink(shm_path);

  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    return 1;
  }
  return 0;
}

int consumer_creates_then_forked_producer_main(const char* shm_path) {
  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = sizeof(xproc::core::control_block) + 8192;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = true;

  xproc::ipc::consumer consumer(opts);
  if (consumer.header()->attach_count.load(std::memory_order_acquire) != 1u) {
    return 2;
  }
  if (consumer.header()->producer_pid.load(std::memory_order_relaxed) != 0) {
    return 3;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }

  if (pid == 0) {
    xproc::ipc::transport_options child_opts = opts;
    {
      xproc::ipc::producer producer(child_opts);
      producer.send_fixed<std::uint32_t>(0x13572468u);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    _exit(0);
  }

  std::uint32_t value = 0;
  bool got = false;
  while (!got) {
    got = consumer.poll([&](void* p, std::uint32_t len) {
      if (len != sizeof(value)) {
        value = 0;
        return;
      }
      std::memcpy(&value, p, sizeof(value));
    });
    if (!got) {
      const std::uint32_t c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
    }
  }

  if (value != 0x13572468u) {
    waitpid(pid, nullptr, 0);
    return 4;
  }
  if (consumer.header()->attach_count.load(std::memory_order_acquire) != 2u) {
    waitpid(pid, nullptr, 0);
    return 5;
  }
  if (consumer.header()->producer_pid.load(std::memory_order_relaxed) != pid) {
    waitpid(pid, nullptr, 0);
    return 6;
  }

  int st = 0;
  waitpid(pid, &st, 0);
  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    return 7;
  }
  if (consumer.header()->attach_count.load(std::memory_order_acquire) != 1u) {
    return 8;
  }
  return 0;
}

}  // namespace

TEST(IpcIntegration, CrossProcessFutexBlock) {
  std::string base = "/xproc_ipc_integration_";
  std::string a = base + "futex";
  xproc::core::shm::unlink(a.c_str());
  EXPECT_EQ(cross_process_futex_block_main(a.c_str()), 0);
}

TEST(IpcIntegration, CrossProcessVarlen) {
  std::string base = "/xproc_ipc_integration_";
  std::string b = base + "varlen";
  xproc::core::shm::unlink(b.c_str());
  EXPECT_EQ(cross_process_varlen_main(b.c_str()), 0);
}

TEST(IpcIntegration, ConsumerCanCreateBeforeForkedProducer) {
  std::string path = "/xproc_ipc_integration_consumer_creator";
  xproc::core::shm::unlink(path.c_str());
  EXPECT_EQ(consumer_creates_then_forked_producer_main(path.c_str()), 0);
  xproc::core::shm::unlink(path.c_str());
}
