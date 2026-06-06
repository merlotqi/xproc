#include <xproc/ipc/diagnostics_tracker.hpp>

namespace xproc::ipc {

void diagnostics_tracker::update(const ring_snapshot& snap) {
  prev_ = curr_;
  curr_ = snap;
  if (curr_.write_pos != prev_.write_pos || curr_.read_pos != prev_.read_pos) {
    last_progress_ = std::chrono::steady_clock::now();
  }
}

std::uint64_t diagnostics_tracker::idle_duration_ms() const {
  const auto elapsed = std::chrono::steady_clock::now() - last_progress_;
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

}  // namespace xproc::ipc
