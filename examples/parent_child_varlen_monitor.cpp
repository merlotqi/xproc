// Parent starts as the consumer and creates the SHM segment up front.
// Child writer thread later attaches as the producer and sends variable-length text messages.
// Parent polls and prints only when payload changes.
//
// Linux: fork + waitpid. Windows: CreateProcess + child re-invokes this exe with a flag and SHM path.

#if defined(__linux__)

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
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
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

#else

#include <iostream>

int main() {
  std::cout << "parent_child_varlen_monitor: unsupported platform\n";
  return 0;
}

#endif

#if defined(__linux__) || defined(_WIN32) || defined(_WIN64)

namespace {

constexpr std::size_t kDataCapacity = 32768;
constexpr std::size_t kShmSize = xproc::ipc::shm_size_for_data_capacity(kDataCapacity);

std::string make_message(int i) {
  const std::size_t burst = 12u + static_cast<std::size_t>((i % 5) * 11);
  return "tick-" + std::to_string(i) + " status=" + ((i % 2) == 0 ? "steady" : "busy") + " payload=" +
         std::string(burst, static_cast<char>('a' + (i % 26)));
}

int run_child_writer(const std::string& shm_path) {
  xproc::ipc::transport_options opts;
  opts.path = shm_path;
  opts.shm_size = xproc::ipc::infer_existing_shm_size;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.create_if_missing = false;

  xproc::ipc::producer producer(opts);

  std::thread writer([&] {
    for (int i = 0; i <= 20; ++i) {
      const std::string msg = make_message(i);
      producer.send_varlen(msg.data(), static_cast<std::uint32_t>(msg.size()));
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

  const std::string path = "/xproc_example_parent_child_varlen";

  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::varlen;
  // Demonstrates the parent consumer creating the segment before forking the producer child.
  opts.create_if_missing = true;
  xproc::ipc::consumer consumer(opts);

  const pid_t pid = fork();
  if (pid < 0) {
    std::perror("fork");
    xproc::core::shm::unlink(path);
    return 1;
  }
  if (pid == 0) {
    _exit(run_child_writer(path));
  }

  std::string last;
  bool has_last = false;
  int status = 0;

  while (true) {
    consumer.poll([&](void* p, std::uint32_t len) {
      const std::string cur(static_cast<const char*>(p), static_cast<std::size_t>(len));
      if (!has_last || cur != last) {
        std::cout << "message(" << len << " bytes)=" << cur << "\n";
        last = cur;
        has_last = true;
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (waitpid(pid, &status, WNOHANG) == pid) {
      break;
    }
  }

  xproc::core::shm::unlink(path);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#elif defined(_WIN32) || defined(_WIN64)

constexpr const char* kChildFlag = "--pc-varlen-child";

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], kChildFlag) == 0) {
    return run_child_writer(std::string(argv[2]));
  }

  const std::string path = std::string("/xproc_example_parent_child_varlen_") +
                           std::to_string(::GetCurrentProcessId());

  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::varlen;
  // Demonstrates the parent consumer creating the segment before launching the producer child.
  opts.create_if_missing = true;
  xproc::ipc::consumer consumer(opts);

  char exe_path[MAX_PATH];
  if (::GetModuleFileNameA(nullptr, exe_path, MAX_PATH) == 0u) {
    std::cerr << "GetModuleFileNameA failed\n";
    xproc::core::shm::unlink(path);
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
    xproc::core::shm::unlink(path);
    return 1;
  }
  ::CloseHandle(pi.hThread);

  std::string last;
  bool has_last = false;
  DWORD exit_code = 1;

  for (;;) {
    consumer.poll([&](void* p, std::uint32_t len) {
      const std::string cur(static_cast<const char*>(p), static_cast<std::size_t>(len));
      if (!has_last || cur != last) {
        std::cout << "message(" << len << " bytes)=" << cur << "\n";
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

  xproc::core::shm::unlink(path);
  if (exit_code != 0) {
    std::cerr << "child process failed\n";
    return 1;
  }
  std::cout << "child exited, parent done\n";
  return 0;
}

#endif

#endif
