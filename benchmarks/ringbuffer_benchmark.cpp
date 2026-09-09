#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <vector>
#include <spscring/fixed_reader.hpp>
#include <spscring/fixed_writer.hpp>
#include <spscring/varlen_reader.hpp>
#include <spscring/varlen_writer.hpp>

namespace {

using spscring::control_block;

// ~1 MiB on the stack trips stack canaries when benchmarks run under CTest / worker threads.
// Heap-backed storage with correct alignment for placement-new of control_block.
class heap_ring_arena {
 public:
  explicit heap_ring_arena(std::uint64_t data_cap, spscring::layout_type layout, std::uint32_t data_align,
                           std::uint32_t fixed_item_size = 0) {
    const std::size_t need = sizeof(control_block) + static_cast<std::size_t>(data_cap);
    const std::size_t align = alignof(control_block);
    storage_.resize(need + align - 1);
    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage_.data());
    std::uintptr_t adj = (base + align - 1) & ~(align - 1);
    hdr_ = reinterpret_cast<control_block*>(adj);
    new (hdr_) control_block{};
    spscring::init_control_block(*hdr_, data_cap, layout, data_align, fixed_item_size);
  }

  heap_ring_arena(const heap_ring_arena&) = delete;
  heap_ring_arena& operator=(const heap_ring_arena&) = delete;

  ~heap_ring_arena() {
    if (hdr_) {
      hdr_->~control_block();
      hdr_ = nullptr;
    }
  }

  control_block* header() noexcept { return hdr_; }

 private:
  std::vector<std::uint8_t> storage_{};
  control_block* hdr_{nullptr};
};

// fixed/varlen single-slot paths use one contiguous get_ptr() region per message. When virtual
// write_pos wraps, a slot can span the physical end of data_capacity — the library assumes that
// does not happen for a valid producer. Benchmarks run millions of iterations; reset each lap.
inline void bench_rewind_ring_positions(control_block* hdr) {
  hdr->rb_meta.write_pos.store(0, std::memory_order_relaxed);
  hdr->rb_meta.read_pos.store(0, std::memory_order_relaxed);
  hdr->rb_meta.commit_pos.store(0, std::memory_order_relaxed);
  hdr->rb_meta.commit_seq.store(0, std::memory_order_relaxed);
  hdr->rb_meta.read_wake_seq.store(0, std::memory_order_relaxed);
}

static void BM_FixedRingReserveCommit(benchmark::State& state) {
  const std::uint32_t item = static_cast<std::uint32_t>(state.range(0));
  if (item == 0 || item > 4096) {
    state.SkipWithError("item size out of bounds");
    return;
  }
  constexpr std::uint64_t cap = 1024 * 1024;
  heap_ring_arena arena(cap, spscring::layout_type::fixed, 8, item);
  control_block* hdr = arena.header();

  spscring::fixed_writer w(hdr);
  spscring::fixed_reader r(hdr);
  static const char pattern[4096]{};

  for (auto _ : state) {
    bench_rewind_ring_positions(hdr);
    void* buf = w.try_reserve();
    std::memcpy(buf, pattern, item);
    w.commit();
    const void* slot = r.try_read();
    bool drained = (slot != nullptr);
    if (!drained) {
      state.SkipWithError("expected immediate read after same-thread commit");
      break;
    }
    benchmark::DoNotOptimize(slot);
    r.read_advance();
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * item));
}

static void BM_VarlenRingReserveCommit(benchmark::State& state) {
  const std::uint32_t len = static_cast<std::uint32_t>(state.range(0));
  if (len == 0 || len > 8192) {
    state.SkipWithError("payload len out of bounds");
    return;
  }
  constexpr std::uint64_t cap = 1024 * 1024;
  heap_ring_arena arena(cap, spscring::layout_type::varlen, 8);
  control_block* hdr = arena.header();

  spscring::varlen_writer w(hdr);
  spscring::varlen_reader rd(hdr);
  std::vector<std::uint8_t> payload(len, std::uint8_t{0x7e});

  for (auto _ : state) {
    bench_rewind_ring_positions(hdr);
    auto rr = w.try_reserve(len);
    if (rr.status != spscring::reserve_status::ok) {
      state.SkipWithError("reserve failed");
      break;
    }
    std::memcpy(rr.payload, payload.data(), len);
    w.commit(rr.position);
    bool drained = rd.read([&](std::uint8_t*& p, std::uint32_t& n, spscring::message_meta&, std::uint64_t&) {
      benchmark::DoNotOptimize(p);
      benchmark::DoNotOptimize(n);
      return true;
    });
    if (!drained) {
      state.SkipWithError("expected immediate read after same-thread commit");
      break;
    }
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * len));
}

}  // namespace

BENCHMARK(BM_FixedRingReserveCommit)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_VarlenRingReserveCommit)->Arg(32)->Arg(256)->Arg(2048)->Unit(benchmark::kNanosecond);
