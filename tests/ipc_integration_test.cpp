// Linux-only: shared memory, fork, and futex cross-process behaviour.

#if !defined(__linux__)

int main() { return 0; }

#else

#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

namespace {

void test_fixed_item_size_zero_rejected() {
  xproc::ipc::transport_options opts;
  opts.path = "/xproc_test_item_zero";
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 0;
  bool threw = false;
  try {
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::producer);
    (void)ch;
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

void test_observer_endpoint_rejected() {
  xproc::ipc::transport_options opts;
  opts.path = "/xproc_test_observer";
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 1024;
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
    xproc::ipc::ipc_endpoint ep(opts, xproc::ipc::ipc_endpoint::role::observer);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    (void)ep;
  } catch (const std::logic_error &e) {
    threw = true;
    const std::string w(e.what());
    assert(w.find("ipc_observer") != std::string::npos);
  }
  assert(threw);
}

void test_consumer_layout_error_includes_reason() {
  const char *path = "/xproc_test_layout_msg";
  xproc::shm::shm::unlink(path);
  xproc::shm::shm sm;
  assert(sm.open(path, sizeof(xproc::shm::shm_control_block) + 512, xproc::shm::shm_open_mode::open_create));
  sm.detach();

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 512;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 4;
  opts.create_if_missing = false;

  bool threw = false;
  try {
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::consumer);
    (void)ch;
  } catch (const xproc::shm::layout_exception &e) {
    threw = true;
    assert(e.code() == xproc::shm::layout_validate_error::bad_magic);
    const std::string w(e.what());
    assert(w.find("bad magic") != std::string::npos);
  }
  assert(threw);
  xproc::shm::shm::unlink(path);
}

void test_shm_size_rejected() {
  xproc::ipc::transport_options opts;
  opts.path = "/xproc_test_shm_too_small";
  opts.shm_size = 1;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = 4;
  bool threw = false;
  try {
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::producer);
    (void)ch;
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

void test_validate_layout_mismatch() {
  xproc::shm::shm_control_block h{};
  using lm = xproc::shm::shm_layout_manager;
  h.magic = lm::EXPECTED_MAGIC;
  h.version_major = lm::VERSION_MAJOR;
  h.version_minor = lm::VERSION_MINOR;
  h.header_size = sizeof(xproc::shm::shm_control_block);
  h.layout_type = 0;  // fixed
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  assert(!lm::validate(&h, 100, 1u, 8u));  // consumer expects variable (1)
}

void test_role_send_poll() {
  constexpr std::size_t total = sizeof(xproc::shm::shm_control_block) + 256;
  xproc::ipc::transport_options o;
  o.path = "/xproc_role_test";
  o.shm_size = total;
  o.type = xproc::ipc::channel_type::fixed;
  o.item_size = 4;
  xproc::shm::shm::unlink(o.path);
  xproc::ipc::ipc_channel prod(o, xproc::ipc::ipc_endpoint::role::producer);
  bool caught = false;
  try {
    prod.poll([](void *, std::uint32_t) {});
  } catch (const std::logic_error &) {
    caught = true;
  }
  assert(caught);
  xproc::shm::shm::unlink(o.path);
}

int cross_process_futex_block_main(const char *shm_path) {
  pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }

  if (pid == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xproc::ipc::transport_options opts;
    opts.path = shm_path;
    opts.shm_size = sizeof(xproc::shm::shm_control_block) + 8192;
    opts.type = xproc::ipc::channel_type::fixed;
    opts.item_size = sizeof(std::uint32_t);
    opts.create_if_missing = false;
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::consumer);

    xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, 0u);

    std::uint32_t val = 0;
    bool got = false;
    while (!got) {
      got = ch.poll([&](void *p, std::uint32_t len) {
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
    opts.shm_size = sizeof(xproc::shm::shm_control_block) + 8192;
    opts.type = xproc::ipc::channel_type::fixed;
    opts.item_size = sizeof(std::uint32_t);
    xproc::ipc::ipc_channel prod(opts, xproc::ipc::ipc_endpoint::role::producer);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    prod.send_fixed(0xdeadbeefu);
    waitpid(pid, &st, 0);
  }
  xproc::shm::shm::unlink(shm_path);

  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    return 1;
  }
  return 0;
}

int cross_process_varlen_main(const char *shm_path) {
  pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }

  const char *msg = "hello-varlen-ipc";

  if (pid == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    xproc::ipc::transport_options opts;
    opts.path = shm_path;
    opts.shm_size = sizeof(xproc::shm::shm_control_block) + 16384;
    opts.type = xproc::ipc::channel_type::variable;
    opts.create_if_missing = false;
    xproc::ipc::ipc_channel ch(opts, xproc::ipc::ipc_endpoint::role::consumer);

    bool got = false;
    while (!got) {
      got = ch.poll([&](void *p, std::uint32_t len) {
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
    opts.shm_size = sizeof(xproc::shm::shm_control_block) + 16384;
    opts.type = xproc::ipc::channel_type::variable;
    xproc::ipc::ipc_channel prod(opts, xproc::ipc::ipc_endpoint::role::producer);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    prod.send_varlen(msg, static_cast<std::uint32_t>(std::strlen(msg)));
    waitpid(pid, &st, 0);
  }
  xproc::shm::shm::unlink(shm_path);

  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  test_fixed_item_size_zero_rejected();
  test_observer_endpoint_rejected();
  test_consumer_layout_error_includes_reason();
  test_shm_size_rejected();
  test_validate_layout_mismatch();
  test_role_send_poll();

  std::string base = "/xproc_ipc_integration_";
  std::string a = base + "futex";
  std::string b = base + "varlen";
  xproc::shm::shm::unlink(a.c_str());
  xproc::shm::shm::unlink(b.c_str());

  if (cross_process_futex_block_main(a.c_str()) != 0) {
    return 1;
  }
  if (cross_process_varlen_main(b.c_str()) != 0) {
    return 1;
  }
  return 0;
}

#endif
