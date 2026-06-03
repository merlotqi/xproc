#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <xproc/ipc/diagnostics_tracker.hpp>
#include <xproc/xproc.hpp>

static xproc::ipc::transport_options make_test_opts(const std::string& path, std::size_t data_capacity) {
  xproc::core::shm::unlink(path);
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = sizeof(xproc::core::control_block) + data_capacity;
  opts.type = xproc::ipc::channel_type::fixed;
  opts.item_size = sizeof(std::uint32_t);
  return opts;
}

TEST(ObserverDiagnostics, OccupancyRatioEmptyRing) {
  const std::string path = "/xproc_diag_occ_ratio_empty";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);
    EXPECT_DOUBLE_EQ(obs.occupancy_ratio(), 0.0);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyRatioPartialFill) {
  const std::string path = "/xproc_diag_occ_ratio_partial";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);
    double ratio = obs.occupancy_ratio();
    EXPECT_GT(ratio, 0.0);
    EXPECT_LT(ratio, 1.0);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyRatioFullRing) {
  const std::string path = "/xproc_diag_occ_ratio_full";
  auto opts = make_test_opts(path, 64);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    while (prod.try_send_fixed<std::uint32_t>(1u)) {
    }
    double ratio = obs.occupancy_ratio();
    EXPECT_GE(ratio, 0.9);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyBytesEmptyRing) {
  const std::string path = "/xproc_diag_occ_bytes_empty";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);
    EXPECT_EQ(obs.occupancy_bytes(), 0u);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, OccupancyBytesAfterSend) {
  const std::string path = "/xproc_diag_occ_bytes_send";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);
    EXPECT_GT(obs.occupancy_bytes(), 0u);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, AvailableBytesEmptyRing) {
  const std::string path = "/xproc_diag_avail_empty";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);
    EXPECT_EQ(obs.available_bytes(), static_cast<std::uint64_t>(opts.shm_size - sizeof(xproc::core::control_block)));
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, ConsumerLagBytesNoConsumer) {
  const std::string path = "/xproc_diag_lag_no_cons";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);
    EXPECT_EQ(obs.consumer_lag_bytes(), obs.occupancy_bytes());
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, ConsumerLagBytesAfterConsume) {
  const std::string path = "/xproc_diag_lag_after_consume";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);
    xproc::ipc::consumer cons(opts);

    prod.send_fixed<std::uint32_t>(42u);
    const auto lag_before = obs.consumer_lag_bytes();
    EXPECT_GT(lag_before, 0u);

    cons.poll([](void*, std::uint32_t) {});
    const auto lag_after = obs.consumer_lag_bytes();
    EXPECT_LT(lag_after, lag_before);
  }
  xproc::core::shm::unlink(path);
}

TEST(ObserverDiagnostics, ObserverDiagnosticsMatchManualCalc) {
  const std::string path = "/xproc_diag_match_manual";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    prod.send_fixed<std::uint32_t>(42u);

    const auto snap = obs.snapshot();
    const auto cap = static_cast<std::uint64_t>(obs.header()->data_capacity);
    const auto used = snap.write_pos - snap.read_pos;
    const auto expected = (used > cap) ? cap : used;

    EXPECT_EQ(obs.occupancy_bytes(), expected);
    EXPECT_EQ(obs.consumer_lag_bytes(), expected);
    EXPECT_DOUBLE_EQ(obs.occupancy_ratio(), static_cast<double>(expected) / static_cast<double>(cap));
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, ProducerAliveOnCommit) {
  const std::string path = "/xproc_diag_tracker_alive";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    prod.send_fixed<std::uint32_t>(42u);
    tracker.update(obs.snapshot());
    EXPECT_TRUE(tracker.producer_alive());
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, ProducerIdleNoActivity) {
  const std::string path = "/xproc_diag_tracker_idle";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    tracker.update(obs.snapshot());
    EXPECT_FALSE(tracker.producer_alive());
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, IdleDurationIncreases) {
  const std::string path = "/xproc_diag_tracker_idle_dur";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(tracker.idle_duration_ms(), 40u);
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, IdleDurationResetsOnProgress) {
  const std::string path = "/xproc_diag_tracker_idle_reset";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap, obs.header()->data_capacity);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    prod.send_fixed<std::uint32_t>(42u);
    tracker.update(obs.snapshot());
    EXPECT_LT(tracker.idle_duration_ms(), 50u);
  }
  xproc::core::shm::unlink(path);
}

TEST(DiagnosticsTracker, MultipleUpdates) {
  const std::string path = "/xproc_diag_tracker_multi";
  auto opts = make_test_opts(path, 512);
  {
    xproc::ipc::producer prod(opts);
    xproc::ipc::observer obs(opts);

    auto snap0 = obs.snapshot();
    xproc::ipc::diagnostics_tracker tracker(snap0, obs.header()->data_capacity);

    prod.send_fixed<std::uint32_t>(1u);
    tracker.update(obs.snapshot());
    const auto prev1 = tracker.previous();
    EXPECT_EQ(prev1.write_pos, snap0.write_pos);

    prod.send_fixed<std::uint32_t>(2u);
    tracker.update(obs.snapshot());
    const auto prev2 = tracker.previous();
    EXPECT_GT(prev2.write_pos, prev1.write_pos);
    EXPECT_GT(tracker.current().write_pos, prev2.write_pos);
  }
  xproc::core::shm::unlink(path);
}
