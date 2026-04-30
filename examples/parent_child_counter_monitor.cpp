// Parent starts child process; child starts a writer thread.
// Child writes [0..100] to IPC on an interval then exits.
// Parent polls, prints only when value changes, exits when child exits.
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
  std::cout << "parent_child_counter_monitor: unsupported platform\n";
  return 0;
}

#endif

#if defined(__linux__) || defined(_WIN32) || defined(_WIN64)

namespace {

constexpr std::size_t kDataCapacity = 16384;
constexpr std::size_t kShmSize = xproc::ipc::shm_size_for_data_capacity(kDataCapacity);

int run_child_writer(const std::string& shm_path) {
  xproc::ipc::transport_options child_opts;
  child_opts.path = shm_path;
  child_opts.shm_size = xproc::ipc::infer_existing_shm_size;
  child_opts.type = xproc::ipc::channel_type::fixed;
  child_opts.item_size = sizeof(std::uint32_t);
  child_opts.create_if_missing = false;

  xproc::ipc::producer producer(child_opts);

  std::thread writer([&] {
    for (std::uint32_t v = 0; v <= 100; ++v) {
      producer.send_fixed(v);
      std::this_thread::sleep_for(std::chrono::seconds(1));
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

  const std::string path = "/xproc_example_parent_child_counter";

  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
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
    const int rc = run_child_writer(path);
    _exit(rc);
  }

  bool has_last = false;
  std::uint32_t last_value = 0;
  int status = 0;

  while (true) {
    const bool got = consumer.poll([&](void* p, std::uint32_t len) {
      if (len != sizeof(std::uint32_t)) {
        return;
      }
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      if (!has_last || v != last_value) {
        std::cout << "value changed: " << v << "\n";
        last_value = v;
        has_last = true;
      }
    });

    if (!got) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

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

constexpr const char* kChildFlag = "--pc-counter-child";

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], kChildFlag) == 0) {
    return run_child_writer(std::string(argv[2]));
  }

  const std::string path =
      std::string("/xproc_example_parent_child_counter_") + std::to_string(::GetCurrentProcessId());

  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = kShmSize;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
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

  bool has_last = false;
  std::uint32_t last_value = 0;
  DWORD exit_code = 1;

  for (;;) {
    const bool got = consumer.poll([&](void* p, std::uint32_t len) {
      if (len != sizeof(std::uint32_t)) {
        return;
      }
      std::uint32_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      if (!has_last || v != last_value) {
        std::cout << "value changed: " << v << "\n";
        last_value = v;
        has_last = true;
      }
    });

    if (!got) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

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
