#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <vector>
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/fixed_writer.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/ringbuffer/varlen_writer.hpp>
#include <xproc/shm/shm_layout_manager.hpp>


namespace {

using xproc::shm::shm_control_block;

// ~1 MiB on the stack trips stack canaries when benchmarks run under CTest / worker threads.
// Heap-backed storage with correct alignment for placement-new of shm_control_block.
class heap_ring_arena {
 public:
  explicit heap_ring_arena(std::uint64_t data_cap, std::uint32_t layout_type, std::uint32_t data_align) {
    const std::size_t need = sizeof(shm_control_block) + static_cast<std::size_t>(data_cap);
    const std::size_t align = alignof(shm_control_block);
    storage_.resize(need + align - 1);
    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage_.data());
    std::uintptr_t adj = (base + align - 1) & ~(align - 1);
    hdr_ = reinterpret_cast<shm_control_block*>(adj);
    new (hdr_) shm_control_block{};
    init_header(*hdr_, data_cap, layout_type, data_align);
  }

  heap_ring_arena(const heap_ring_arena&) = delete;
  heap_ring_arena& operator=(const heap_ring_arena&) = delete;

  ~heap_ring_arena() {
    if (hdr_) {
      hdr_->~shm_control_block();
      hdr_ = nullptr;
    }
  }

  shm_control_block* header() noexcept { return hdr_; }

 private:
  static void init_header(shm_control_block& h, std::uint64_t cap, std::uint32_t layout_type,
                          std::uint32_t data_align) {
    using xproc::shm::shm_layout_manager;
    h.magic = shm_layout_manager::EXPECTED_MAGIC;
    h.version_major = shm_layout_manager::VERSION_MAJOR;
    h.version_minor = shm_layout_manager::VERSION_MINOR;
    h.header_size = sizeof(shm_control_block);
    h.layout_type = layout_type;
    h.rb_meta.write_pos.store(0, std::memory_order_relaxed);
    h.rb_meta.read_pos.store(0, std::memory_order_relaxed);
    h.rb_meta.commit_seq.store(0, std::memory_order_relaxed);
    h.rb_meta.read_wake_seq.store(0, std::memory_order_relaxed);
    h.data_capacity = cap;
    h.data_alignment = data_align ? data_align : 8u;
    h.attach_count.store(0, std::memory_order_relaxed);
    h.is_ready.store(true, std::memory_order_release);
    h.producer_pid.store(0, std::memory_order_relaxed);
  }

  std::vector<std::uint8_t> storage_{};
  shm_control_block* hdr_{nullptr};
};

// fixed/varlen single-slot paths use one contiguous get_ptr() region per message. When virtual
// write_pos wraps, a slot can span the physical end of data_capacity — the library assumes that
// does not happen for a valid producer. Benchmarks run millions of iterations; reset each lap.
inline void bench_rewind_ring_positions(shm_control_block* hdr) {
  hdr->rb_meta.write_pos.store(0, std::memory_order_relaxed);
  hdr->rb_meta.read_pos.store(0, std::memory_order_relaxed);
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
  heap_ring_arena arena(cap, 0, 8);
  shm_control_block* hdr = arena.header();

  xproc::ringbuffer::fixed_writer w(hdr);
  xproc::ringbuffer::fixed_reader r(hdr);
  static const char pattern[4096]{};
  std::uint64_t pos = 0;

  for (auto _ : state) {
    bench_rewind_ring_positions(hdr);
    void* buf = w.reserve(item, pos);
    std::memcpy(buf, pattern, item);
    w.commit(pos);
    bool drained = r.try_read(item, [](void* p) { benchmark::DoNotOptimize(p); });
    if (!drained) {
      state.SkipWithError("expected immediate read after same-thread commit");
      break;
    }
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
  heap_ring_arena arena(cap, 1, 8);
  shm_control_block* hdr = arena.header();

  xproc::ringbuffer::varlen_writer w(hdr);
  xproc::ringbuffer::varlen_reader rd(hdr);
  std::vector<std::uint8_t> payload(len, std::uint8_t{0x7e});
  std::uint64_t pos = 0;

  for (auto _ : state) {
    bench_rewind_ring_positions(hdr);
    void* buf = w.reserve(len, pos);
    std::memcpy(buf, payload.data(), len);
    w.commit(pos);
    bool drained = rd.try_read([&](void* p, std::uint32_t n) {
      benchmark::DoNotOptimize(p);
      benchmark::DoNotOptimize(n);
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
