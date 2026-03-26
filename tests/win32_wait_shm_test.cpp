// Windows: WaitOnAddress / WakeByAddress, named file mapping, and cross-process commit_seq (CMake: WIN32 only).

#if !defined(_WIN32)

int main() { return 0; }

#else

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <xproc/ipc/ipc_channel.hpp>
#include <xproc/ipc/ipc_observer.hpp>
#include <xproc/platform/process.hpp>
#include <xproc/shm/shm.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace {

constexpr std::size_t kShmTotal = sizeof(xproc::shm::shm_control_block) + 8192;

int run_win_ipc_child(const char *shm_path) {
  ::Sleep(400);
  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = kShmTotal;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  opts.create_if_missing = false;
  xproc::ipc::consumer_channel ch(opts);

  xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, 0u);

  std::uint32_t val = 0;
  bool got = false;
  while (!got) {
    got = ch.poll([&](void *p, std::uint32_t len) {
      (void)len;
      std::memcpy(&val, p, sizeof(val));
    });
    if (!got) {
      const std::uint32_t c = ch.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
      xproc::sync::atomic_wait(&ch.header()->rb_meta.commit_seq, c);
    }
  }
  return (val == 0xdeadbeefu) ? 0 : 2;
}

void test_atomic_wait_notify_thread() {
  alignas(8) std::atomic<std::uint32_t> word{0u};
  bool progressed = false;
  std::thread waiter([&] {
    xproc::sync::atomic_wait(&word, 0u);
    progressed = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  word.store(1u, std::memory_order_release);
  xproc::sync::atomic_notify_one(&word);
  waiter.join();
  assert(progressed);
}

void test_shm_producer_observer_peek() {
  const std::string path = "/xproc_win32_shm_" + std::to_string(xproc::platform::current_process_id());
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmTotal;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);

  {
    xproc::ipc::producer_channel prod(opts);
    xproc::ipc::ipc_observer obs(opts);
    prod.send_fixed<std::uint32_t>(0x11223344u);
    bool ok = false;
    for (int i = 0; i < 5000 && !ok; ++i) {
      ok = obs.peek([&](const void *p, std::uint32_t len) {
        assert(len == 4u);
        std::uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        assert(v == 0x11223344u);
      });
      if (!ok) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
    }
    assert(ok);
  }

  xproc::shm::shm::unlink(path);
}

void test_cross_process_commit_seq() {
  std::string path = "/xproc_win32_ipc_" + std::to_string(::GetCurrentProcessId());
  xproc::shm::shm::unlink(path);

  char exe_path[MAX_PATH];
  assert(::GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0);

  xproc::ipc::transport_options po;
  po.path = path;
  po.shm_size = kShmTotal;
  po.type = xproc::ipc::channel_type::fixed;
  po.item_size = sizeof(std::uint32_t);
  po.create_if_missing = true;

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  {
    xproc::ipc::producer_channel prod(po);

    std::string cmdline = std::string("\"") + exe_path + "\" --win-ipc-child \"" + path + "\"";
    std::vector<char> cmd_mut(cmdline.begin(), cmdline.end());
    cmd_mut.push_back('\0');

    const BOOL ok = ::CreateProcessA(exe_path, cmd_mut.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    assert(ok);
    ::CloseHandle(pi.hThread);

    ::Sleep(600);
    prod.send_fixed<std::uint32_t>(0xdeadbeefu);

    assert(::WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0);
    DWORD code = 1;
    assert(::GetExitCodeProcess(pi.hProcess, &code));
    ::CloseHandle(pi.hProcess);
    assert(code == 0);
  }

  xproc::shm::shm::unlink(path);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc >= 3 && std::strcmp(argv[1], "--win-ipc-child") == 0) {
    return run_win_ipc_child(argv[2]);
  }

  test_atomic_wait_notify_thread();
  test_shm_producer_observer_peek();
  test_cross_process_commit_seq();
  return 0;
}

#endif
