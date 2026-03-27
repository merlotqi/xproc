#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <xproc/ipc/ipc_options.hpp>
#include <xproc/platform/process.hpp>
#include <xproc/shm/layout_exception.hpp>
#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout.hpp>

#include <xproc/shm/shm_layout_manager.hpp>
#include <xproc/shm/shm_open_mode.hpp>

namespace xproc {
namespace ipc {

class ipc_endpoint {
 public:
  enum class role {
    producer,
    consumer,
    observer [[deprecated("use xproc::ipc::ipc_observer for read-only attach")]]
  };

  explicit ipc_endpoint(const transport_options& opts, role rle) : role_(rle), opts_(opts) { establish_connection(); }

  ~ipc_endpoint() {
    if (header_) {
      header_->attach_count.fetch_sub(1, std::memory_order_acq_rel);
    }
  }

  ipc_endpoint(const ipc_endpoint&) = delete;
  ipc_endpoint& operator=(const ipc_endpoint&) = delete;

  role user_role() const { return role_; }
  bool is_connect() const { return header_ != nullptr; }
  shm::shm_control_block* header() const { return header_; }
  transport_options options() const { return opts_; }

 protected:
  role role_;
  shm::shm shm_;
  shm::shm_control_block* header_{nullptr};
  transport_options opts_;

 private:
  void establish_connection() {
    using role_ut = std::underlying_type_t<role>;
    // Reject deprecated observer without naming role::observer (avoids -Wdeprecated-declarations in every TU).
    constexpr role_ut k_observer_ut = static_cast<role_ut>(role::consumer) + 1;
    if (static_cast<role_ut>(role_) == k_observer_ut) {
      throw std::logic_error(
          "ipc_endpoint: observer role is not supported here; use ipc_observer (read-only attach) instead");
    }

    validate_transport_options(opts_);

    using namespace xproc::shm;
    shm_open_mode mode = shm_open_mode::open;
    if (role_ == role::producer && opts_.create_if_missing) {
      mode = shm_open_mode::open_create;
    }

    if (!shm_.open(opts_.path, opts_.shm_size, mode, opts_.win32_object_namespace)) {
      std::string msg = "ipc_endpoint: failed to attach shm path: " + opts_.path;
      const int err = shm_.last_os_error();
      if (err != 0) {
        msg += " (os_error=";
        msg += std::to_string(err);
        msg += ")";
      }
      throw std::runtime_error(msg);
    }

    bool is_creater = (role_ == role::producer);

    size_t data_capacity = opts_.shm_size - sizeof(shm_control_block);
    const uint32_t layout_type = (opts_.type == channel_type::fixed) ? 0u : 1u;
    const uint32_t data_align = opts_.data_align ? opts_.data_align : 8u;
    header_ = shm_layout_manager::format(shm_, data_capacity, is_creater, layout_type, data_align);
    if (!header_) {
      const auto* raw = static_cast<const shm_control_block*>(shm_.addr());
      const auto err = shm_layout_manager::validate_detailed(raw, data_capacity, layout_type, data_align);
      throw layout_exception("ipc_endpoint: ", err);
    }

    if (role_ == role::producer) {
      header_->producer_pid.store(xproc::platform::current_process_id(), std::memory_order_relaxed);
    }
  }
};

}  // namespace ipc
}  // namespace xproc
