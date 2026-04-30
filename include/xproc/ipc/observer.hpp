#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <xproc/ipc/inspector.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/shm/layout_exception.hpp>
#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout_manager.hpp>

namespace xproc {
namespace ipc {

// Read-only attach: does not advance read_pos. Uses readonly: does not bump attach_count; attach_count()
// in the control block only reflects producer/consumer writable mappings. attach_count_view_interface here is a
// read-only view of that field for metrics—not evidence that this observer incremented the counter.
class observer : public ring_inspector_interface, public attach_count_view_interface {
 public:
  explicit observer(const transport_options& opts) : opts_(opts) {
    validate_observer_transport_options(opts_);
    if (!shm_.open(opts_.path, opts_.shm_size, shm::shm_open_mode::read, opts_.win32_object_namespace)) {
      std::string msg = "observer: failed to attach shm path: " + opts_.path;
      const int err = shm_.last_os_error();
      if (err != 0) {
        msg += " (os_error=";
        msg += std::to_string(err);
        msg += ")";
      }
      throw std::runtime_error(msg);
    }

    const std::size_t data_capacity = shm_data_capacity_for_size(opts_.shm_size);
    const std::uint32_t layout_type = (opts_.type == channel_type::fixed) ? 0u : 1u;
    const std::uint32_t data_align = opts_.data_align ? opts_.data_align : 8u;
    const std::uint32_t fixed_item_size = (opts_.type == channel_type::fixed) ? opts_.item_size : 0u;
    header_ = shm::layout_manager::format(shm_, data_capacity, false, layout_type, data_align, fixed_item_size,
                                          opts_.schema_id, opts_.creator_timestamp_ns, opts_.creator_flags,
                                          shm::attach_behavior::readonly);
    if (!header_) {
      const auto* raw = static_cast<const shm::control_block*>(shm_.addr());
      const auto err = shm::layout_manager::validate_detailed(raw, data_capacity, layout_type, data_align,
                                                              fixed_item_size, opts_.schema_id);
      throw shm::layout_exception("observer: ", err);
    }

    opts_.shm_size = shm_size_for_data_capacity(static_cast<std::size_t>(header_->data_capacity));
    opts_.creator_timestamp_ns = header_->creator_timestamp_ns;
    opts_.creator_flags = header_->creator_flags;

    if (opts_.type == channel_type::fixed) {
      fixed_reader_ = std::make_unique<ringbuffer::fixed_reader>(header_);
    } else {
      varlen_reader_ = std::make_unique<ringbuffer::varlen_reader>(header_);
    }
  }

  ~observer() override = default;

  observer(const observer&) = delete;
  observer& operator=(const observer&) = delete;

  const transport_options& options() const noexcept { return opts_; }

  shm::control_block* header() noexcept { return header_; }
  const shm::control_block* header() const noexcept { return header_; }

  ring_snapshot snapshot() const override {
    ring_snapshot s;
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

  // Fixed: handler(const void *payload, uint32_t len) with len == item_size. Variable: same as channel::poll.
  template <typename F>
  bool peek(F&& handler) {
    if (opts_.type == channel_type::fixed) {
      return fixed_reader_->peek(opts_.item_size, std::forward<F>(handler));
    }
    return varlen_reader_->peek(std::forward<F>(handler));
  }

 private:
  transport_options opts_;
  shm::shm shm_;
  shm::control_block* header_{nullptr};
  std::unique_ptr<ringbuffer::fixed_reader> fixed_reader_;
  std::unique_ptr<ringbuffer::varlen_reader> varlen_reader_;
};

}  // namespace ipc
}  // namespace xproc
