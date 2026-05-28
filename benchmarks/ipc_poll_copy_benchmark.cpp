#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <xproc/xproc.hpp>

namespace {

std::string unique_path(const char* prefix, int arg) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::string("/xproc_bench_") + prefix + "_" + std::to_string(arg) + "_" + std::to_string(now);
}

// Mirrors ipc_runtime's poll handler: copy into std::vector then touch first byte (executor would move it).
static void BM_FixedPollCopyLikeRuntime(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  if (payload_len == 0 || payload_len > 4096) {
    state.SkipWithError("payload_len out of bounds");
    return;
  }

  const std::string path = unique_path("rtcopy", static_cast<int>(payload_len));
  xproc::core::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + 512 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  std::vector<std::byte> payload(payload_len, std::byte{0x91});

  for (auto _ : state) {
    producer.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    bool got = false;
    while (!got) {
      got = consumer.poll([&](void* ptr, std::uint32_t len) {
        std::vector<std::uint8_t> copy(static_cast<std::uint8_t*>(ptr), static_cast<std::uint8_t*>(ptr) + len);
        benchmark::DoNotOptimize(copy.data());
        benchmark::DoNotOptimize(copy.size());
      });
      if (!got) {
        const auto c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::core::shm::unlink(path);
}

}  // namespace

BENCHMARK(BM_FixedPollCopyLikeRuntime)->Arg(16)->Arg(128)->Arg(1024)->Unit(benchmark::kMicrosecond);
