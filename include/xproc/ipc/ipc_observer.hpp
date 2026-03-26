#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <xproc/ipc/ipc_inspector.hpp>
#include <xproc/ipc/ipc_options.hpp>
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/shm/layout_exception.hpp>
#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout_manager.hpp>

namespace xproc {
namespace ipc {

// Read-only attach: does not advance read_pos. Uses observe_only: does not bump attach_count; attach_count()
// in the control block only reflects producer/consumer writable mappings. IIpcAttachCountView here is a
// read-only view of that field for metrics—not evidence that this observer incremented the counter.
class ipc_observer : public IIpcRingInspector, public IIpcAttachCountView {
 public:
  explicit ipc_observer(const transport_options &opts) : opts_(opts) {
    validate_transport_options(opts_);
    if (!shm_.open(opts_.path, opts_.shm_size, shm::shm_open_mode::read)) {
      std::string msg = "ipc_observer: failed to attach shm path: " + opts_.path;
      const int err = shm_.last_os_error();
      if (err != 0) {
        msg += " (os_error=";
        msg += std::to_string(err);
        msg += ")";
      }
      throw std::runtime_error(msg);
    }

    const std::size_t data_capacity = opts_.shm_size - sizeof(shm::shm_control_block);
    const std::uint32_t layout_type = (opts_.type == channel_type::fixed) ? 0u : 1u;
    const std::uint32_t data_align = opts_.data_align ? opts_.data_align : 8u;
    header_ = shm::shm_layout_manager::format(shm_, data_capacity, false, layout_type, data_align,
                                              shm::layout_attach_behavior::observe_only);
    if (!header_) {
      const auto *raw = static_cast<const shm::shm_control_block *>(shm_.addr());
      const auto err = shm::shm_layout_manager::validate_detailed(raw, data_capacity, layout_type, data_align);
      throw shm::layout_exception("ipc_observer: ", err);
    }

    if (opts_.type == channel_type::fixed) {
      fixed_reader_ = std::make_unique<ringbuffer::fixed_reader>(header_);
    } else {
      varlen_reader_ = std::make_unique<ringbuffer::varlen_reader>(header_);
    }
  }

  ~ipc_observer() override = default;

  ipc_observer(const ipc_observer &) = delete;
  ipc_observer &operator=(const ipc_observer &) = delete;

  const transport_options &options() const noexcept { return opts_; }

  shm::shm_control_block *header() noexcept { return header_; }
  const shm::shm_control_block *header() const noexcept { return header_; }

  ipc_ring_snapshot ring_snapshot() const override {
    ipc_ring_snapshot s;
    if (!header_) {
      return s;
    }
    s.write_pos = header_->rb_meta.write_pos.load(std::memory_order_acquire);
    s.read_pos = header_->rb_meta.read_pos.load(std::memory_order_acquire);
    s.commit_seq = header_->rb_meta.commit_seq.load(std::memory_order_acquire);
    s.read_wake_seq = header_->rb_meta.read_wake_seq.load(std::memory_order_acquire);
    s.attach_count = header_->attach_count.load(std::memory_order_acquire);
    s.producer_pid = header_->producer_pid.load(std::memory_order_relaxed);
    return s;
  }

  std::uint32_t attach_count() const noexcept override {
    // Read-only attach does not participate in attach_count bump/decrement; value reflects producer/consumer refs.
    return header_ ? header_->attach_count.load(std::memory_order_acquire) : 0;
  }

  // Fixed: handler(const void *payload, uint32_t len) with len == item_size. Variable: same as ipc_channel::poll.
  template <typename F>
  bool peek(F &&handler) {
    if (opts_.type == channel_type::fixed) {
      return fixed_reader_->try_peek(opts_.item_size, std::forward<F>(handler));
    }
    return varlen_reader_->try_peek(std::forward<F>(handler));
  }

 private:
  transport_options opts_;
  shm::shm shm_;
  shm::shm_control_block *header_{nullptr};
  std::unique_ptr<ringbuffer::fixed_reader> fixed_reader_;
  std::unique_ptr<ringbuffer::varlen_reader> varlen_reader_;
};

}  // namespace ipc
}  // namespace xproc
