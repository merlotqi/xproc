#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <xproc/core/shm.hpp>
#include <xproc/platform/process.hpp>
#include <xproc/sync/atomic_backoff.hpp>
#include <xproc/sync/atomic_wait.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
constexpr const char* k_win32_child_flag = "--win32-atomic-wait-child";
constexpr const wchar_t* k_win32_child_flag_w = L"--win32-atomic-wait-child";
constexpr std::uint32_t k_stop_request = 0xffffffffu;

struct cross_process_wait_block {
  alignas(XPROC_CACHE_LINE_SIZE) std::atomic<std::uint32_t> ready{0};
  std::uint8_t padding1[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<std::uint32_t>)];

  alignas(XPROC_CACHE_LINE_SIZE) std::atomic<std::uint32_t> request{0};
  std::uint8_t padding2[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<std::uint32_t>)];

  alignas(XPROC_CACHE_LINE_SIZE) std::atomic<std::uint32_t> response{0};
  std::uint8_t padding3[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<std::uint32_t>)];
};

struct win32_process {
  HANDLE process{nullptr};
  HANDLE thread{nullptr};

  win32_process() = default;
  win32_process(const win32_process&) = delete;
  win32_process& operator=(const win32_process&) = delete;

  win32_process(win32_process&& other) noexcept : process(other.process), thread(other.thread) {
    other.process = nullptr;
    other.thread = nullptr;
  }

  win32_process& operator=(win32_process&& other) noexcept {
    if (this != &other) {
      close();
      process = other.process;
      thread = other.thread;
      other.process = nullptr;
      other.thread = nullptr;
    }
    return *this;
  }

  ~win32_process() { close(); }

  void close() {
    if (thread != nullptr) {
      ::CloseHandle(thread);
      thread = nullptr;
    }
    if (process != nullptr) {
      ::CloseHandle(process);
      process = nullptr;
    }
  }
};

std::string unique_path(const char* prefix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::string("/xproc_bench_") + prefix + "_" + std::to_string(xproc::platform::current_process_id()) + "_" +
         std::to_string(now);
}

std::wstring utf8_to_wide(const std::string& value) {
  if (value.empty()) {
    return std::wstring{};
  }
  const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
  if (needed <= 0) {
    throw std::runtime_error("utf8_to_wide: MultiByteToWideChar size failed");
  }
  std::wstring out(static_cast<std::size_t>(needed), L'\0');
  const int written =
      ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), needed);
  if (written != needed) {
    throw std::runtime_error("utf8_to_wide: MultiByteToWideChar conversion failed");
  }
  return out;
}

std::wstring current_executable_path() {
  wchar_t exe_path[MAX_PATH];
  const DWORD len = ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (len == 0 || len == MAX_PATH) {
    throw std::runtime_error("current_executable_path: GetModuleFileNameW failed");
  }
  return std::wstring(exe_path, exe_path + len);
}

win32_process spawn_wait_child(const std::string& shm_path) {
  const std::wstring exe_path = current_executable_path();
  const std::wstring shm_path_w = utf8_to_wide(shm_path);
  std::wstring cmdline = std::wstring(L"\"") + exe_path + L"\" " + k_win32_child_flag_w + L" \"" + shm_path_w + L"\"";
  std::vector<wchar_t> cmd_mut(cmdline.begin(), cmdline.end());
  cmd_mut.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!::CreateProcessW(exe_path.c_str(), cmd_mut.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    throw std::runtime_error("spawn_wait_child: CreateProcessW failed");
  }

  win32_process child;
  child.process = pi.hProcess;
  child.thread = pi.hThread;
  return child;
}

int run_cross_process_wait_child(const char* shm_path) {
  xproc::core::shm mapping;
  if (!mapping.open(shm_path, sizeof(cross_process_wait_block), xproc::core::shm_open_mode::open)) {
    return 2;
  }

  auto* block = static_cast<cross_process_wait_block*>(mapping.addr());
  block->ready.store(1u, std::memory_order_release);
  xproc::sync::atomic_notify_one(&block->ready);

  std::uint32_t seen = block->request.load(std::memory_order_acquire);
  while (true) {
    xproc::sync::atomic_wait(&block->request, seen);
    const std::uint32_t next = block->request.load(std::memory_order_acquire);
    if (next == seen) {
      continue;
    }
    if (next == k_stop_request) {
      return 0;
    }
    seen = next;
    block->response.store(next, std::memory_order_release);
    xproc::sync::atomic_notify_one(&block->response);
  }
}

void publish_wait_delta_counters(benchmark::State& state, const xproc::sync::atomic_wait_win32_stats& before,
                                 const xproc::sync::atomic_wait_win32_stats& after) {
  const std::uint64_t wait_calls = after.wait_calls - before.wait_calls;
  const std::uint64_t polling_wait_calls = after.polling_wait_calls - before.polling_wait_calls;
  const std::uint64_t native_wait_calls = after.native_wait_calls - before.native_wait_calls;
  const std::uint64_t native_timeout_count = after.native_timeout_count - before.native_timeout_count;
  const std::uint64_t spin_iterations = after.spin_iterations - before.spin_iterations;
  const std::uint64_t yield_iterations = after.yield_iterations - before.yield_iterations;
  const std::uint64_t polling_sleep_calls = after.polling_sleep_calls - before.polling_sleep_calls;
  const std::uint64_t notify_one_calls = after.notify_one_calls - before.notify_one_calls;
  const std::uint64_t notify_all_calls = after.notify_all_calls - before.notify_all_calls;

  const double ops = state.iterations() > 0 ? static_cast<double>(state.iterations()) : 1.0;

  state.counters["win32_wait_calls"] = static_cast<double>(wait_calls);
  state.counters["win32_polling_wait_calls"] = static_cast<double>(polling_wait_calls);
  state.counters["win32_native_wait_calls"] = static_cast<double>(native_wait_calls);
  state.counters["win32_native_timeout_count"] = static_cast<double>(native_timeout_count);
  state.counters["win32_spin_iterations"] = static_cast<double>(spin_iterations);
  state.counters["win32_yield_iterations"] = static_cast<double>(yield_iterations);
  state.counters["win32_polling_sleep_calls"] = static_cast<double>(polling_sleep_calls);
  state.counters["win32_notify_one_calls"] = static_cast<double>(notify_one_calls);
  state.counters["win32_notify_all_calls"] = static_cast<double>(notify_all_calls);

  state.counters["win32_wait_calls_per_op"] = static_cast<double>(wait_calls) / ops;
  state.counters["win32_native_wait_calls_per_op"] = static_cast<double>(native_wait_calls) / ops;
  state.counters["win32_native_timeout_per_op"] = static_cast<double>(native_timeout_count) / ops;
  state.counters["win32_spin_iterations_per_op"] = static_cast<double>(spin_iterations) / ops;
  state.counters["win32_yield_iterations_per_op"] = static_cast<double>(yield_iterations) / ops;
}
#endif

// Exercise the spin portion of atomic_backoff (CPU pause / yield) without blocking on atomic_wait.
static void BM_AtomicBackoffSpinOnly(benchmark::State& state) {
  const int inner = static_cast<int>(state.range(0));
  std::atomic<std::uint32_t> seq{0};
  xproc::sync::atomic_backoff backoff(1'000'000);

  for (auto _ : state) {
    backoff.reset();
    for (int i = 0; i < inner; ++i) {
      backoff.pause(seq, seq.load(std::memory_order_relaxed));
    }
    benchmark::DoNotOptimize(seq.load(std::memory_order_relaxed));
  }

  state.SetItemsProcessed(state.iterations() * inner);
}

// Exercise the full wait/notify path with a helper thread that blocks on atomic_wait and
// acknowledges each wake by publishing the same sequence number back.
static void BM_AtomicWaitThreadPingPong(benchmark::State& state) {
  std::atomic<std::uint32_t> request{0};
  std::atomic<std::uint32_t> response{0};
  std::atomic<bool> stop{false};

  std::thread waiter([&] {
    std::uint32_t seen = 0;
    while (true) {
      xproc::sync::atomic_wait(&request, seen);
      const std::uint32_t next = request.load(std::memory_order_acquire);
      if (next == seen) {
        continue;
      }
      if (stop.load(std::memory_order_acquire)) {
        return;
      }
      seen = next;
      response.store(next, std::memory_order_release);
      xproc::sync::atomic_notify_one(&response);
    }
  });

#ifdef _WIN32
  const xproc::sync::atomic_wait_win32_stats wait_stats_before = xproc::sync::atomic_wait_win32_get_stats();
#endif

  for (auto _ : state) {
    const std::uint32_t next = request.load(std::memory_order_relaxed) + 1;
    request.store(next, std::memory_order_release);
    xproc::sync::atomic_notify_one(&request);

    while (true) {
      const std::uint32_t ack = response.load(std::memory_order_acquire);
      if (ack == next) {
        break;
      }
      xproc::sync::atomic_wait(&response, ack);
    }
  }

  stop.store(true, std::memory_order_release);
  request.store(request.load(std::memory_order_relaxed) + 1, std::memory_order_release);
  xproc::sync::atomic_notify_one(&request);
  waiter.join();

  state.SetItemsProcessed(state.iterations());

#ifdef _WIN32
  const xproc::sync::atomic_wait_win32_stats wait_stats_after = xproc::sync::atomic_wait_win32_get_stats();
  publish_wait_delta_counters(state, wait_stats_before, wait_stats_after);
#endif
}

#ifdef _WIN32
static void BM_AtomicWaitCrossProcessPingPong(benchmark::State& state) {
  const std::string path = unique_path("atomic_wait_cross_process");
  xproc::core::shm::unlink(path);

  xproc::core::shm mapping;
  if (!mapping.open(path, sizeof(cross_process_wait_block), xproc::core::shm_open_mode::open_create)) {
    state.SkipWithError("BM_AtomicWaitCrossProcessPingPong: failed to create shared mapping");
    return;
  }

  auto* block = ::new (mapping.addr()) cross_process_wait_block{};

  win32_process child;
  try {
    child = spawn_wait_child(path);
  } catch (const std::exception& ex) {
    state.SkipWithError(ex.what());
    return;
  }

  while (block->ready.load(std::memory_order_acquire) == 0u) {
    xproc::sync::atomic_wait(&block->ready, 0u);
  }

  const xproc::sync::atomic_wait_win32_stats wait_stats_before = xproc::sync::atomic_wait_win32_get_stats();

  std::uint32_t next = 0;
  for (auto _ : state) {
    const auto begin = std::chrono::steady_clock::now();
    ++next;
    block->request.store(next, std::memory_order_release);
    xproc::sync::atomic_notify_one(&block->request);

    while (true) {
      const std::uint32_t ack = block->response.load(std::memory_order_acquire);
      if (ack == next) {
        break;
      }
      xproc::sync::atomic_wait(&block->response, ack);
    }
    const auto end = std::chrono::steady_clock::now();
    state.SetIterationTime(std::chrono::duration<double>(end - begin).count());
  }

  block->request.store(k_stop_request, std::memory_order_release);
  xproc::sync::atomic_notify_one(&block->request);
  if (::WaitForSingleObject(child.process, 30000) != WAIT_OBJECT_0) {
    state.SkipWithError("BM_AtomicWaitCrossProcessPingPong: child did not exit");
    return;
  }
  DWORD exit_code = 1;
  if (!::GetExitCodeProcess(child.process, &exit_code) || exit_code != 0u) {
    state.SkipWithError("BM_AtomicWaitCrossProcessPingPong: child exit failure");
    return;
  }

  state.SetItemsProcessed(state.iterations());
  const xproc::sync::atomic_wait_win32_stats wait_stats_after = xproc::sync::atomic_wait_win32_get_stats();
  publish_wait_delta_counters(state, wait_stats_before, wait_stats_after);
}
#endif

}  // namespace

BENCHMARK(BM_AtomicBackoffSpinOnly)->Arg(64)->Arg(512)->Arg(4096)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_AtomicWaitThreadPingPong)->Unit(benchmark::kMicrosecond)->UseRealTime();
#ifdef _WIN32
BENCHMARK(BM_AtomicWaitCrossProcessPingPong)->Unit(benchmark::kMicrosecond)->UseManualTime();
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  if (argc >= 3 && std::strcmp(argv[1], k_win32_child_flag) == 0) {
    return run_cross_process_wait_child(argv[2]);
  }
#endif

  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}
