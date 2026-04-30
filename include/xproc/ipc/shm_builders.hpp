#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <xproc/ipc/channel.hpp>
#include <xproc/ipc/observer.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/shm/layout_exception.hpp>
#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout_manager.hpp>
#include <xproc/shm/shm_open_mode.hpp>

namespace xproc {
namespace ipc {

namespace detail {

inline std::string make_shm_attach_error(const char* context, const std::string& path, int os_error) {
  std::string msg = std::string(context) + "failed to attach shm path: " + path;
  if (os_error != 0) {
    msg += " (os_error=";
    msg += std::to_string(os_error);
    msg += ")";
  }
  return msg;
}

inline transport_options read_existing_shm_options(const std::string& path, const std::string& win32_object_namespace,
                                                   const char* context) {
  shm::shm mapping;
  // On Windows, shared memory may not be immediately available after creation;
  // retry a few times with small delays to handle timing issues.
  const int max_retries = 10;
  bool opened = false;
  for (int i = 0; i < max_retries && !opened; ++i) {
    if (mapping.open(path, infer_existing_shm_size, shm::shm_open_mode::read, win32_object_namespace)) {
      opened = true;
    } else {
      if (i + 1 < max_retries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }
  if (!opened) {
    throw std::runtime_error(make_shm_attach_error(context, path, mapping.last_os_error()));
  }

  const auto* header = static_cast<const shm::control_block*>(mapping.addr());
  std::uint32_t layout_type = 0u;
  std::uint32_t data_align = 8u;
  std::uint32_t fixed_item_size = 0u;
  std::uint64_t schema_id = 0u;
  std::uint64_t creator_timestamp_ns = 0u;
  std::uint64_t creator_flags = 0u;

  if (header != nullptr) {
    layout_type = header->layout_type;
    data_align = header->data_alignment ? header->data_alignment : 8u;
    fixed_item_size = header->fixed_item_size;
    schema_id = header->schema_id;
    creator_timestamp_ns = header->creator_timestamp_ns;
    creator_flags = header->creator_flags;
  }

  const auto err =
      shm::layout_manager::validate_detailed(header, 0u, layout_type, data_align, fixed_item_size, schema_id);
  if (err != shm::validate_error::ok) {
    throw shm::layout_exception(context, err);
  }
  if (layout_type != 0u && layout_type != 1u) {
    throw shm::layout_exception(context, shm::validate_error::layout_type_mismatch);
  }
  if (layout_type == 0u && fixed_item_size == 0u) {
    throw shm::layout_exception(context, shm::validate_error::fixed_item_size_mismatch);
  }

  const std::size_t logical_shm_size =
      shm_size_for_data_capacity(static_cast<std::size_t>(header->data_capacity));

  transport_options opts;
  opts.path = path;
  // Report the creator's logical segment size, not the OS-mapped region size. On Windows the
  // mapped section can be rounded up to page granularity, which would otherwise break re-attach
  // validation by overstating the expected data capacity.
  opts.shm_size = logical_shm_size;
  opts.item_size = (layout_type == 0u) ? fixed_item_size : 0u;
  opts.data_align = data_align;
  opts.schema_id = schema_id;
  opts.creator_timestamp_ns = creator_timestamp_ns;
  opts.creator_flags = creator_flags;
  opts.create_if_missing = false;
  opts.type = (layout_type == 0u) ? channel_type::fixed : channel_type::varlen;
  opts.win32_object_namespace = win32_object_namespace;
  validate_transport_options(opts);
  return opts;
}

}  // namespace detail

// Stores one SHM channel configuration and can open producer / consumer / observer endpoints from it.
class shm_channel_endpoints {
 public:
  explicit shm_channel_endpoints(transport_options opts) : opts_(std::move(opts)) {}

  const transport_options& options() const noexcept { return opts_; }

  transport_options producer_options() const { return opts_; }

  transport_options consumer_options() const { return opts_; }

  transport_options observer_options() const {
    transport_options opts = opts_;
    opts.create_if_missing = false;
    return opts;
  }

  ipc::producer open_producer() const { return ipc::producer(producer_options()); }

  ipc::consumer open_consumer() const { return ipc::consumer(consumer_options()); }

  ipc::observer open_observer() const { return ipc::observer(observer_options()); }

 private:
  transport_options opts_;
};

class fixed_channel_builder {
 public:
  fixed_channel_builder(std::string path, std::uint32_t item_size)
      : path_(std::move(path)), item_size_(item_size) {}

  fixed_channel_builder& with_data_align(std::uint32_t data_align) {
    data_align_ = data_align;
    return *this;
  }

  fixed_channel_builder& with_schema_id(std::uint64_t schema_id) {
    schema_id_ = schema_id;
    return *this;
  }

  fixed_channel_builder& with_creator_timestamp_ns(std::uint64_t creator_timestamp_ns) {
    creator_timestamp_ns_ = creator_timestamp_ns;
    return *this;
  }

  fixed_channel_builder& with_creator_flags(std::uint64_t creator_flags) {
    creator_flags_ = creator_flags;
    return *this;
  }

  fixed_channel_builder& with_win32_object_namespace(std::string win32_object_namespace) {
    win32_object_namespace_ = std::move(win32_object_namespace);
    return *this;
  }

  transport_options options(std::size_t data_capacity) const {
    transport_options opts;
    opts.path = path_;
    opts.shm_size = shm_size_for_data_capacity(data_capacity);
    opts.item_size = item_size_;
    opts.data_align = data_align_;
    opts.schema_id = schema_id_;
    opts.creator_timestamp_ns = creator_timestamp_ns_;
    opts.creator_flags = creator_flags_;
    opts.create_if_missing = true;
    opts.type = channel_type::fixed;
    opts.win32_object_namespace = win32_object_namespace_;
    validate_transport_options(opts);
    return opts;
  }

  shm_channel_endpoints create(std::size_t data_capacity) const { return shm_channel_endpoints(options(data_capacity)); }

 private:
  std::string path_;
  std::uint32_t item_size_{0};
  std::uint32_t data_align_{0};
  std::uint64_t schema_id_{0};
  std::uint64_t creator_timestamp_ns_{0};
  std::uint64_t creator_flags_{0};
  std::string win32_object_namespace_{"Local"};
};

class varlen_channel_builder {
 public:
  explicit varlen_channel_builder(std::string path) : path_(std::move(path)) {}

  varlen_channel_builder& with_data_align(std::uint32_t data_align) {
    data_align_ = data_align;
    return *this;
  }

  varlen_channel_builder& with_schema_id(std::uint64_t schema_id) {
    schema_id_ = schema_id;
    return *this;
  }

  varlen_channel_builder& with_creator_timestamp_ns(std::uint64_t creator_timestamp_ns) {
    creator_timestamp_ns_ = creator_timestamp_ns;
    return *this;
  }

  varlen_channel_builder& with_creator_flags(std::uint64_t creator_flags) {
    creator_flags_ = creator_flags;
    return *this;
  }

  varlen_channel_builder& with_win32_object_namespace(std::string win32_object_namespace) {
    win32_object_namespace_ = std::move(win32_object_namespace);
    return *this;
  }

  transport_options options(std::size_t data_capacity) const {
    transport_options opts;
    opts.path = path_;
    opts.shm_size = shm_size_for_data_capacity(data_capacity);
    opts.item_size = 0u;
    opts.data_align = data_align_;
    opts.schema_id = schema_id_;
    opts.creator_timestamp_ns = creator_timestamp_ns_;
    opts.creator_flags = creator_flags_;
    opts.create_if_missing = true;
    opts.type = channel_type::varlen;
    opts.win32_object_namespace = win32_object_namespace_;
    validate_transport_options(opts);
    return opts;
  }

  shm_channel_endpoints create(std::size_t data_capacity) const { return shm_channel_endpoints(options(data_capacity)); }

 private:
  std::string path_;
  std::uint32_t data_align_{0};
  std::uint64_t schema_id_{0};
  std::uint64_t creator_timestamp_ns_{0};
  std::uint64_t creator_flags_{0};
  std::string win32_object_namespace_{"Local"};
};

class fixed_channel_attacher {
 public:
  explicit fixed_channel_attacher(std::string path) : path_(std::move(path)) {}

  fixed_channel_attacher& with_schema_id(std::uint64_t schema_id) {
    schema_id_ = schema_id;
    schema_id_set_ = true;
    return *this;
  }

  fixed_channel_attacher& with_win32_object_namespace(std::string win32_object_namespace) {
    win32_object_namespace_ = std::move(win32_object_namespace);
    return *this;
  }

  transport_options options() const {
    transport_options opts =
        detail::read_existing_shm_options(path_, win32_object_namespace_, "attach_fixed_channel: ");
    if (opts.type != channel_type::fixed) {
      throw shm::layout_exception("attach_fixed_channel: ", shm::validate_error::layout_type_mismatch);
    }
    if (schema_id_set_ && opts.schema_id != schema_id_) {
      throw shm::layout_exception("attach_fixed_channel: ", shm::validate_error::schema_id_mismatch);
    }
    if (schema_id_set_) {
      opts.schema_id = schema_id_;
    }
    return opts;
  }

  ipc::producer open_producer() const { return ipc::producer(options()); }

  ipc::consumer open_consumer() const { return ipc::consumer(options()); }

  ipc::observer open_observer() const { return ipc::observer(options()); }

 private:
  std::string path_;
  std::uint64_t schema_id_{0};
  bool schema_id_set_{false};
  std::string win32_object_namespace_{"Local"};
};

class varlen_channel_attacher {
 public:
  explicit varlen_channel_attacher(std::string path) : path_(std::move(path)) {}

  varlen_channel_attacher& with_schema_id(std::uint64_t schema_id) {
    schema_id_ = schema_id;
    schema_id_set_ = true;
    return *this;
  }

  varlen_channel_attacher& with_win32_object_namespace(std::string win32_object_namespace) {
    win32_object_namespace_ = std::move(win32_object_namespace);
    return *this;
  }

  transport_options options() const {
    transport_options opts =
        detail::read_existing_shm_options(path_, win32_object_namespace_, "attach_varlen_channel: ");
    if (opts.type != channel_type::varlen) {
      throw shm::layout_exception("attach_varlen_channel: ", shm::validate_error::layout_type_mismatch);
    }
    if (schema_id_set_ && opts.schema_id != schema_id_) {
      throw shm::layout_exception("attach_varlen_channel: ", shm::validate_error::schema_id_mismatch);
    }
    if (schema_id_set_) {
      opts.schema_id = schema_id_;
    }
    return opts;
  }

  ipc::producer open_producer() const { return ipc::producer(options()); }

  ipc::consumer open_consumer() const { return ipc::consumer(options()); }

  ipc::observer open_observer() const { return ipc::observer(options()); }

 private:
  std::string path_;
  std::uint64_t schema_id_{0};
  bool schema_id_set_{false};
  std::string win32_object_namespace_{"Local"};
};

inline fixed_channel_builder make_fixed_channel(std::string path, std::uint32_t item_size) {
  return fixed_channel_builder(std::move(path), item_size);
}

inline varlen_channel_builder make_varlen_channel(std::string path) { return varlen_channel_builder(std::move(path)); }

inline fixed_channel_attacher attach_fixed_channel(std::string path) {
  return fixed_channel_attacher(std::move(path));
}

inline varlen_channel_attacher attach_varlen_channel(std::string path) {
  return varlen_channel_attacher(std::move(path));
}

}  // namespace ipc
}  // namespace xproc
