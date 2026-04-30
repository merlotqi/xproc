#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <xproc/core/layout_exception.hpp>
#include <xproc/core/shm.hpp>
#include <xproc/core/shm_layout.hpp>
#include <xproc/core/shm_layout_manager.hpp>
#include <xproc/core/shm_open_mode.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/platform/process.hpp>

namespace xproc::ipc {

class endpoint {
 public:
  enum class role {
    producer,
    consumer,
    observer [[deprecated("use xproc::ipc::observer for read-only attach")]]
  };

  explicit endpoint(const transport_options& opts, role rle) : role_(rle), opts_(opts) { establish_connection(); }

  ~endpoint() {
    if (header_) {
      header_->attach_count.fetch_sub(1, std::memory_order_acq_rel);
    }
  }

  endpoint(const endpoint&) = delete;
  endpoint& operator=(const endpoint&) = delete;

  role get_role() const { return role_; }
  bool is_connected() const { return header_ != nullptr; }
  core::control_block* header() const { return header_; }
  const transport_options& options() const { return opts_; }

 protected:
  role role_;
  core::shm shm_;
  core::control_block* header_{nullptr};
  transport_options opts_;

 private:
  void establish_connection() {
    using role_ut = std::underlying_type_t<role>;
    // Reject deprecated observer without naming role::observer (avoids -Wdeprecated-declarations in every TU).
    constexpr role_ut k_observer_ut = static_cast<role_ut>(role::consumer) + 1;
    if (static_cast<role_ut>(role_) == k_observer_ut) {
      throw std::logic_error(
          "endpoint: observer role is not supported here; use xproc::ipc::observer for read-only attach instead");
    }

    if (role_ == role::producer) {
      validate_producer_transport_options(opts_);
    } else if (role_ == role::consumer) {
      validate_consumer_transport_options(opts_);
    } else {
      validate_transport_options(opts_);
    }

    using namespace xproc::core;
    shm_open_mode mode = shm_open_mode::open;
    if (opts_.create_if_missing) {
      mode = shm_open_mode::open_create;
    }

    if (!shm_.open(opts_.path, opts_.shm_size, mode, opts_.win32_object_namespace)) {
      std::string msg = "endpoint: failed to attach shm path: " + opts_.path;
      const int err = shm_.last_os_error();
      if (err != 0) {
        msg += " (os_error=";
        msg += std::to_string(err);
        msg += ")";
      }
      throw std::runtime_error(msg);
    }

    const bool is_creator = shm_.created_this_open();

    const size_t data_capacity = shm_data_capacity_for_size(opts_.shm_size);
    const uint32_t layout_type = (opts_.type == channel_type::fixed) ? 0u : 1u;
    const uint32_t data_align = opts_.data_align ? opts_.data_align : 8u;
    const uint32_t fixed_item_size = (opts_.type == channel_type::fixed) ? opts_.item_size : 0u;
    header_ = layout_manager::format(shm_, data_capacity, is_creator, layout_type, data_align, fixed_item_size,
                                     opts_.schema_id, opts_.creator_timestamp_ns, opts_.creator_flags);
    if (!header_) {
      const auto* raw = static_cast<const control_block*>(shm_.addr());
      const auto err = layout_manager::validate_detailed(raw, data_capacity, layout_type, data_align, fixed_item_size,
                                                         opts_.schema_id);
      throw layout_exception("endpoint: ", err);
    }

    opts_.shm_size = shm_size_for_data_capacity(static_cast<std::size_t>(header_->data_capacity));
    opts_.creator_timestamp_ns = header_->creator_timestamp_ns;
    opts_.creator_flags = header_->creator_flags;

    if (is_creator && role_ != role::producer) {
      header_->producer_pid.store(0, std::memory_order_relaxed);
    }
    if (role_ == role::producer) {
      header_->producer_pid.store(xproc::platform::current_process_id(), std::memory_order_relaxed);
    }
  }
};

}  // namespace ipc::xproc
