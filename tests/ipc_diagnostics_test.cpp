#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <thread>
#include <chrono>
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
