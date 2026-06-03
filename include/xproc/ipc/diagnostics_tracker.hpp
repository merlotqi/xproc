#pragma once

#include <chrono>
#include <cstdint>
#include <xproc/ipc/inspector.hpp>

namespace xproc::ipc {

class diagnostics_tracker {
 public:
  explicit diagnostics_tracker(const ring_snapshot& initial, std::uint64_t data_capacity)
      : prev_(initial),
        curr_(initial),
        data_capacity_(data_capacity),
        last_progress_(std::chrono::steady_clock::now()) {}

  void update(const ring_snapshot& snap);

  bool producer_alive() const noexcept { return curr_.commit_seq != prev_.commit_seq; }

  std::uint64_t idle_duration_ms() const;

  const ring_snapshot& current() const noexcept { return curr_; }
  const ring_snapshot& previous() const noexcept { return prev_; }
  std::uint64_t data_capacity() const noexcept { return data_capacity_; }

 private:
  ring_snapshot prev_;
  ring_snapshot curr_;
  std::uint64_t data_capacity_;
  std::chrono::steady_clock::time_point last_progress_;
};

}  // namespace xproc::ipc
