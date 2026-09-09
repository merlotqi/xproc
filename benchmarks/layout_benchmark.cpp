#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <xproc/core/layout_manager.hpp>

namespace {

template <std::size_t N>
struct alignas(xproc::core::control_block) ring_arena {
  std::array<std::uint8_t, N> bytes{};
};

void init_valid_header(xproc::core::control_block& h, std::uint64_t cap, std::uint32_t layout_type,
                       std::uint32_t data_align) {
  using xproc::core::layout_manager;
  h.spscring_cb.magic = layout_manager::expected_magic;
  h.spscring_cb.version_major = layout_manager::version_major;
  h.spscring_cb.version_minor = layout_manager::version_minor;
  h.spscring_cb.header_size = sizeof(xproc::core::control_block);
  h.spscring_cb.layout_type = layout_type;
  h.spscring_cb.rb_meta.write_pos.store(0, std::memory_order_relaxed);
  h.spscring_cb.rb_meta.read_pos.store(0, std::memory_order_relaxed);
  h.spscring_cb.rb_meta.commit_seq.store(0, std::memory_order_relaxed);
  h.spscring_cb.rb_meta.read_wake_seq.store(0, std::memory_order_relaxed);
  h.spscring_cb.data_capacity = cap;
  h.spscring_cb.data_alignment = data_align ? data_align : 8u;
  h.attach_count.store(1, std::memory_order_relaxed);
  h.is_ready.store(true, std::memory_order_release);
  h.producer_pid.store(0, std::memory_order_relaxed);
}

static void BM_ShmLayoutValidate(benchmark::State& state) {
  const std::uint32_t layout_type = static_cast<std::uint32_t>(state.range(0));
  constexpr std::uint64_t cap = 65536;
  constexpr std::size_t total = sizeof(xproc::core::control_block) + static_cast<std::size_t>(cap);
  ring_arena<total> arena{};
  auto* hdr = reinterpret_cast<xproc::core::control_block*>(arena.bytes.data());
  new (hdr) xproc::core::control_block{};
  init_valid_header(*hdr, cap, layout_type, 8);

  const std::size_t expected_cap = static_cast<std::size_t>(cap);

  for (auto _ : state) {
    const bool ok = xproc::core::layout_manager::validate(hdr, expected_cap, layout_type, 8u);
    volatile bool sink = ok;
    benchmark::DoNotOptimize(sink);
  }

  state.SetItemsProcessed(state.iterations());
}

}  // namespace

// 0 = fixed ring layout, 1 = variable-length layout (same validate path, different expected_layout_type).
BENCHMARK(BM_ShmLayoutValidate)->Arg(0)->Arg(1)->Unit(benchmark::kNanosecond);
