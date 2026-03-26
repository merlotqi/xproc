#include <benchmark/benchmark.h>

#include <atomic>
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

static void BM_FixedSendPoll(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  if (payload_len == 0 || payload_len > 1024) {
    state.SkipWithError("payload_len out of expected bounds");
    return;
  }

  const std::string path = unique_path("fixed", static_cast<int>(payload_len));
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = static_cast<std::uint32_t>(payload_len);
  opts.create_if_missing = true;

  xproc::ipc::producer_channel producer(opts);
  xproc::ipc::consumer_channel consumer(opts);
  std::vector<std::byte> payload(payload_len, std::byte{0x5a});

  for (auto _ : state) {
    producer.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    bool got = false;
    while (!got) {
      got = consumer.poll([&](void*, std::uint32_t len) { benchmark::DoNotOptimize(len); });
      if (!got) {
        const auto c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::shm::shm::unlink(path);
}

static void BM_VarlenSendPoll(benchmark::State& state) {
  const std::size_t payload_len = static_cast<std::size_t>(state.range(0));
  if (payload_len == 0 || payload_len > 4096) {
    state.SkipWithError("payload_len out of expected bounds");
    return;
  }

  const std::string path = unique_path("varlen", static_cast<int>(payload_len));
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 1 * 1024 * 1024;
  opts.type = xproc::ipc::channel_type::variable;
  opts.create_if_missing = true;

  xproc::ipc::producer_channel producer(opts);
  xproc::ipc::consumer_channel consumer(opts);
  std::vector<std::byte> payload(payload_len, std::byte{0x42});

  for (auto _ : state) {
    producer.send_varlen(payload.data(), static_cast<std::uint32_t>(payload_len));
    bool got = false;
    while (!got) {
      got = consumer.poll([&](void*, std::uint32_t len) { benchmark::DoNotOptimize(len); });
      if (!got) {
        const auto c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::shm::shm::unlink(path);
}

static void BM_SendEncodedRawPod(benchmark::State& state) {
  const std::string path = unique_path("encoded", 0);
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::shm::shm_control_block) + 512 * 1024;
  opts.type = xproc::ipc::channel_type::variable;
  opts.create_if_missing = true;

  xproc::ipc::producer_channel producer(opts);
  xproc::ipc::consumer_channel consumer(opts);
  std::uint64_t value = 0;

  for (auto _ : state) {
    xproc::ipc::send_encoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(producer, value++);
    bool got = false;
    while (!got) {
      got = xproc::ipc::poll_decoded<xproc::protocol::raw_pod_codec<std::uint64_t>>(consumer,
                                                                                    [&](const std::uint64_t& v) {
                                                                                      std::uint64_t sink = v;
                                                                                      benchmark::DoNotOptimize(sink);
                                                                                    });
      if (!got) {
        const auto c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * sizeof(std::uint64_t)));
  xproc::shm::shm::unlink(path);
}

}  // namespace

BENCHMARK(BM_FixedSendPoll)->Arg(16)->Arg(64)->Arg(256)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_VarlenSendPoll)->Arg(32)->Arg(128)->Arg(1024)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SendEncodedRawPod)->Unit(benchmark::kMicrosecond);
