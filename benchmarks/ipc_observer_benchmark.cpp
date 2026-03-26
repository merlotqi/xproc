#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <xproc/xproc.hpp>

namespace {

std::string unique_path(const char* prefix, int arg) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::string("/xproc_bench_") + prefix + "_" + std::to_string(arg) + "_" + std::to_string(now);
}

// One fixed message stays in the ring; peek does not consume — measures try_peek hot path over SHM.
static void BM_ObserverPeekFixed(benchmark::State& state) {
  const std::uint32_t item = static_cast<std::uint32_t>(state.range(0));
  if (item == 0 || item > 1024) {
    state.SkipWithError("item size out of bounds");
    return;
  }

  const std::string path = unique_path("obs", static_cast<int>(item));
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 256 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = item;
  opts.create_if_missing = true;

  xproc::ipc::producer_channel producer(opts);
  xproc::ipc::ipc_observer observer(opts);

  std::vector<std::byte> payload(item, std::byte{0x3c});
  producer.send_fixed_bytes(payload.data(), item);

  for (auto _ : state) {
    bool ok = observer.peek([&](const void* p, std::uint32_t len) {
      benchmark::DoNotOptimize(p);
      benchmark::DoNotOptimize(len);
    });
    if (!ok) {
      state.SkipWithError("peek expected message to be present");
      break;
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * item));
  xproc::shm::shm::unlink(path);
}

}  // namespace

BENCHMARK(BM_ObserverPeekFixed)->Arg(4)->Arg(64)->Arg(256)->Unit(benchmark::kNanosecond);
