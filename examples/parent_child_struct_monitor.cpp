// Parent starts as the consumer and creates the SHM segment up front.
// Child writer thread later attaches as the producer and sends a fixed struct on an interval.
// Parent polls and prints only when payload changes.
//
// Linux: fork + waitpid. Windows: CreateProcess + child re-invokes this exe with a flag and SHM path.

#if defined(__linux__)

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

#elif defined(_WIN32) || defined(_WIN64)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

#else

#include <iostream>

int main() {
  std::cout << "parent_child_struct_monitor: unsupported platform\n";
  return 0;
}

#endif

#if defined(__linux__) || defined(_WIN32) || defined(_WIN64)

namespace {

struct telemetry_packet {
  char message[256];
  int a;
  int b;
};

constexpr std::size_t kDataCapacity = 32768;
constexpr std::size_t kShmSize = xproc::ipc::shm_size_for_data_capacity(kDataCapacity);

int run_child_writer(const std::string& shm_path) {
  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = xproc::ipc::infer_existing_shm_size;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(telemetry_packet);
  opts.create_if_missing = false;

  xproc::ipc::producer producer(opts);

  std::thread writer([&] {
    for (int i = 0; i <= 100; ++i) {
      telemetry_packet pkt{};
      std::snprintf(pkt.message, sizeof(pkt.message), "tick-%d", i);
      pkt.a = i;
      pkt.b = i * 2;
      producer.send_fixed(pkt);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
  });

  writer.join();
  return 0;
}

}  // namespace

#if defined(__linux__)

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  const std::string path = "/xproc_example_parent_child_struct";

  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(telemetry_packet);
  // Demonstrates the parent consumer creating the segment before forking the producer child.
  opts.create_if_missing = true;
  xproc::ipc::consumer consumer(opts);

  const pid_t pid = fork();
  if (pid < 0) {
    std::perror("fork");
    xproc::shm::shm::unlink(path);
    return 1;
  }
  if (pid == 0) {
    _exit(run_child_writer(path));
  }

  telemetry_packet last{};
  bool has_last = false;
  int status = 0;

  while (true) {
    consumer.poll([&](void* p, std::uint32_t len) {
      if (len != sizeof(telemetry_packet)) {
        return;
      }
      telemetry_packet cur{};
      std::memcpy(&cur, p, sizeof(cur));
      const bool changed = !has_last || std::memcmp(&cur, &last, sizeof(cur)) != 0;
      if (changed) {
        std::cout << "message=" << cur.message << ", a=" << cur.a << ", b=" << cur.b << "\n";
        last = cur;
        has_last = true;
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (waitpid(pid, &status, WNOHANG) == pid) {
      break;
    }
  }

  xproc::shm::shm::unlink(path);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#elif defined(_WIN32) || defined(_WIN64)

constexpr const char* kChildFlag = "--pc-struct-child";

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], kChildFlag) == 0) {
    return run_child_writer(std::string(argv[2]));
  }

  const std::string path = std::string("/xproc_example_parent_child_struct_") + std::to_string(::GetCurrentProcessId());

  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(telemetry_packet);
  // Demonstrates the parent consumer creating the segment before launching the producer child.
  opts.create_if_missing = true;
  xproc::ipc::consumer consumer(opts);

  char exe_path[MAX_PATH];
  if (::GetModuleFileNameA(nullptr, exe_path, MAX_PATH) == 0u) {
    std::cerr << "GetModuleFileNameA failed\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::string cmdline = std::string("\"") + exe_path + "\" " + kChildFlag + " \"" + path + "\"";
  std::vector<char> cmd_mut(cmdline.begin(), cmdline.end());
  cmd_mut.push_back('\0');

  if (!::CreateProcessA(exe_path, cmd_mut.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    std::cerr << "CreateProcessA failed\n";
    xproc::shm::shm::unlink(path);
    return 1;
  }
  ::CloseHandle(pi.hThread);

  telemetry_packet last{};
  bool has_last = false;
  DWORD exit_code = 1;

  for (;;) {
    consumer.poll([&](void* p, std::uint32_t len) {
      if (len != sizeof(telemetry_packet)) {
        return;
      }
      telemetry_packet cur{};
      std::memcpy(&cur, p, sizeof(cur));
      const bool changed = !has_last || std::memcmp(&cur, &last, sizeof(cur)) != 0;
      if (changed) {
        std::cout << "message=" << cur.message << ", a=" << cur.a << ", b=" << cur.b << "\n";
        last = cur;
        has_last = true;
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (::WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
      break;
    }
  }

  if (!::GetExitCodeProcess(pi.hProcess, &exit_code)) {
    exit_code = 1;
  }
  ::CloseHandle(pi.hProcess);

  xproc::shm::shm::unlink(path);
  if (exit_code != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#endif

#endif
