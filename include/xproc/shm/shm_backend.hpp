#pragma once

#include <cstddef>
#include <string>
#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_open_mode.hpp>

namespace xproc {
namespace shm {

// Optional abstraction for tests or alternate backends; endpoint uses xproc::shm::shm directly.
// Subclass and inject where you introduce a transport factory (not wired into channel by default).
class IShmBackend {
 public:
  virtual ~IShmBackend() = default;
  virtual bool open(const std::string& name, std::size_t size, shm_open_mode mode) = 0;
  virtual void detach() = 0;
  virtual void* addr() const = 0;
  virtual std::size_t size() const = 0;
  virtual bool is_attached() const = 0;
  virtual int last_os_error() const = 0;
};

class default_shm_backend final : public IShmBackend {
 public:
  bool open(const std::string& name, std::size_t size, shm_open_mode mode) override {
    return impl_.open(name, size, mode);
  }
  void detach() override { impl_.detach(); }
  void* addr() const override { return impl_.addr(); }
  std::size_t size() const override { return impl_.size(); }
  bool is_attached() const override { return impl_.is_attached(); }
  int last_os_error() const override { return impl_.last_os_error(); }

 private:
  shm impl_;
};

}  // namespace shm
}  // namespace xproc
