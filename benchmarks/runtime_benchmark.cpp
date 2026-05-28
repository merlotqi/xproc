#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

std::string unique_path(const char* prefix, int arg) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::string("/xproc_bench_rt_") + prefix + "_" + std::to_string(arg) +
         "_" + std::to_string(now);
}

// Baseline: direct poll loop without runtime (no intermediate allocation).
static void BM_RuntimeBaselineDirectPoll(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("baseline", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  std::vector<std::byte> payload(payload_len, std::byte{0x5a});

  for (auto _ : state) {
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    bool got = false;
    while (!got) {
      got = cons.poll([&](void*, std::uint32_t len) { benchmark::DoNotOptimize(len); });
      if (!got) {
        const auto c = cons.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&cons.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

// Runtime with reuse_buffer (default) policy — amortized zero allocation.
static void BM_RuntimeReuseBuffer(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("reuse", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> done{true};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t*, std::size_t len) {
             benchmark::DoNotOptimize(len);
             done.store(true);
           },
           xproc::ipc::copy_policy::reuse_buffer);
  });

  std::vector<std::byte> payload(payload_len, std::byte{0x5a});
  for (auto _ : state) {
    done.store(false);
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    while (!done.load()) {
    }
  }

  rt.stop();
  rt_thread.join();
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

// Runtime with zero_copy policy — ring buffer pointer passed directly.
static void BM_RuntimeZeroCopy(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("zero", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> done{true};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t*, std::size_t len) {
             benchmark::DoNotOptimize(len);
             done.store(true);
           },
           xproc::ipc::copy_policy::zero_copy);
  });

  std::vector<std::byte> payload(payload_len, std::byte{0x5a});
  for (auto _ : state) {
    done.store(false);
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    while (!done.load()) {
    }
  }

  rt.stop();
  rt_thread.join();
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

// Runtime with sbo policy (message fits in 256 B stack buffer, zero heap).
static void BM_RuntimeSbo(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  const std::string path = unique_path("sbo", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer prod(opts);
  xproc::ipc::consumer cons(opts);
  xproc::ipc::runtime rt(cons);

  std::atomic<bool> done{true};
  std::thread rt_thread([&] {
    auto executor = [](auto task) { task(); };
    rt.run(executor,
           [&](const std::uint8_t*, std::size_t len) {
             benchmark::DoNotOptimize(len);
             done.store(true);
           },
           xproc::ipc::copy_policy::sbo);
  });

  std::vector<std::byte> payload(payload_len, std::byte{0x5a});
  for (auto _ : state) {
    done.store(false);
    prod.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    while (!done.load()) {
    }
  }

  rt.stop();
  rt_thread.join();
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

}  // namespace

BENCHMARK(BM_RuntimeBaselineDirectPoll)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RuntimeReuseBuffer)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RuntimeZeroCopy)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RuntimeSbo)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
