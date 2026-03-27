#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <xproc/sync/atomic_backoff.hpp>


namespace {

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

}  // namespace

BENCHMARK(BM_AtomicBackoffSpinOnly)->Arg(64)->Arg(512)->Arg(4096)->Unit(benchmark::kNanosecond);
