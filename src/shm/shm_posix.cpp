#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <xproc/shm/shm.hpp>

#if defined(_WIN32)
#error shm_posix.cpp must not be built on Windows
#endif

namespace xproc {
namespace shm {

shm::shm(shm&& other) noexcept { *this = std::move(other); }

shm& shm::operator=(shm&& other) noexcept {
  if (this != &other) {
    detach();

    addr_ = other.addr_;
    size_ = other.size_;
    fd_ = other.fd_;
    last_os_error_ = other.last_os_error_;
    name_ = std::move(other.name_);

    other.addr_ = nullptr;
    other.size_ = 0;
    other.fd_ = -1;
    other.last_os_error_ = 0;
  }
  return *this;
}

bool shm::open(const std::string& name, size_t size, shm_open_mode mode, const std::string& win32_object_namespace) {
  (void)win32_object_namespace;
  last_os_error_ = 0;
  name_ = name;
  size_ = size;

  int oflag = 0;
  if (mode == shm_open_mode::create) {
    // Exclusive create: fail if the name already exists.
    oflag = O_CREAT | O_RDWR | O_EXCL;
  } else if (mode == shm_open_mode::open) {
    oflag = O_RDWR;
  } else if (mode == shm_open_mode::open_create) {
    // Try opening first; if it fails (ENOENT or EACCES from missing), fall through to create below.
    oflag = O_RDWR;
  } else if (mode == shm_open_mode::read) {
    oflag = O_RDONLY;
  }

  fd_ = shm_open(name_.c_str(), oflag, 0666);

  // open_create fallback: if open failed and mode is open_create, try to create.
  if (fd_ == -1 && mode == shm_open_mode::open_create) {
    fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
  }

  if (fd_ == -1) {
    last_os_error_ = errno;
    return false;
  }

  if (mode != shm_open_mode::read) {
    if (ftruncate(fd_, size_) == -1) {
      last_os_error_ = errno;
      ::close(fd_);
      fd_ = -1;
      return false;
    }
  }

  int port = PROT_READ | (mode == shm_open_mode::read ? 0 : PROT_WRITE);
  addr_ = mmap(nullptr, size_, port, MAP_SHARED, fd_, 0);

  if (addr_ == MAP_FAILED) {
    last_os_error_ = errno;
    addr_ = nullptr;
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

void shm::detach() {
  if (addr_) {
    munmap(addr_, size_);
    addr_ = nullptr;
  }

  if (fd_ != -1) {
    ::close(fd_);
    fd_ = -1;
  }
}

void shm::unlink(const std::string& name) {
  // Best-effort unlink; callers should not rely on success (e.g., on Windows unlink is no-op).
  // We intentionally ignore ENOENT (already removed race) but log via last_error for diagnostics.
  shm_unlink(name.c_str());
}

}  // namespace shm
}  // namespace xproc
