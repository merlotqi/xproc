#pragma once

#include <cstddef>
#include <string>
#include <xproc/shm/shm_open_mode.hpp>

namespace xproc {
namespace shm {

class shm {
 public:
  explicit shm() = default;
  ~shm() { detach(); }

  shm(const shm&) = delete;
  shm& operator=(const shm&) = delete;

  shm(shm&&) noexcept;
  shm& operator=(shm&&) noexcept;

  bool open(const std::string& name, size_t size, shm_open_mode mode);
  void detach();

  static void unlink(const std::string& name);

  void* addr() const { return addr_; }
  size_t size() const { return size_; }
  bool is_attached() const { return addr_ != nullptr; }

  // After a failed open(): POSIX errno, or Windows GetLastError() as int; 0 if unset.
  int last_os_error() const noexcept { return last_os_error_; }

 private:
  void* addr_{nullptr};
  size_t size_{0};
  int last_os_error_{0};
#if defined(_WIN32)
  void* mapping_{nullptr};
#else
  int fd_{-1};
#endif
  std::string name_;
};

}  // namespace shm
}  // namespace xproc
