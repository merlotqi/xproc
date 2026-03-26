#pragma once

#include <cstdint>

namespace xproc {
namespace ipc {

// Read-only snapshot of ring control fields (shared with producer/consumer).
struct ipc_ring_snapshot {
  std::uint64_t write_pos{0};
  std::uint64_t read_pos{0};
  std::uint32_t commit_seq{0};
  std::uint32_t read_wake_seq{0};
  std::uint32_t attach_count{0};
  std::int32_t producer_pid{0};
};

// Narrow interface for tooling / metrics: no channel send or consume.
class IIpcRingInspector {
 public:
  virtual ~IIpcRingInspector() = default;
  virtual ipc_ring_snapshot ring_snapshot() const = 0;
};

// Read-only view of attach_count in the shared control block (never modifies the counter).
// Producer/consumer endpoints bump/decrement via shm_layout_manager::format(count_ref) and ~ipc_endpoint;
// ipc_observer uses observe_only and does not participate in that refcount (RO mapping cannot store).
class IIpcAttachCountView {
 public:
  virtual ~IIpcAttachCountView() = default;
  virtual std::uint32_t attach_count() const noexcept = 0;
};

}  // namespace ipc
}  // namespace xproc
