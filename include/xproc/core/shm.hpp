#pragma once

#include <cstddef>
#include <string>
#include <xproc/core/shm_open_mode.hpp>

namespace xproc::core {

class shm {
 public:
  explicit shm() = default;
  ~shm() { detach(); }

  shm(const shm&) = delete;
  shm& operator=(const shm&) = delete;

  shm(shm&&) noexcept;
  shm& operator=(shm&&) noexcept;

  // win32_object_namespace: Windows only ("Local" or "Global"); ignored on POSIX.
  bool open(const std::string& name, size_t size, shm_open_mode mode,
            const std::string& win32_object_namespace = "Local");
  void detach();

  static void unlink(const std::string& name);

  void* addr() const { return addr_; }
  size_t size() const { return size_; }
  bool is_attached() const { return addr_ != nullptr; }
  bool created_this_open() const noexcept { return created_this_open_; }

  // After a failed open(): POSIX errno, or Windows GetLastError() as int; 0 if unset.
  int last_os_error() const noexcept { return last_os_error_; }

 private:
  void* addr_{nullptr};
  size_t size_{0};
  int last_os_error_{0};
  bool created_this_open_{false};
#if defined(_WIN32)
  void* mapping_{nullptr};
  std::string win32_view_key_{};
#else
  int fd_{-1};
#endif
  std::string name_;
};

}  // namespace xproc::core
