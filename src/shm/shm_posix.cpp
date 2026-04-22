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
    created_this_open_ = other.created_this_open_;
    name_ = std::move(other.name_);

    other.addr_ = nullptr;
    other.size_ = 0;
    other.fd_ = -1;
    other.last_os_error_ = 0;
    other.created_this_open_ = false;
  }
  return *this;
}

bool shm::open(const std::string& name, size_t size, shm_open_mode mode, const std::string& win32_object_namespace) {
  (void)win32_object_namespace;
  last_os_error_ = 0;
  created_this_open_ = false;
  name_ = name;
  size_ = size;

  const auto close_and_fail = [&](int err) {
    last_os_error_ = err;
    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }
    return false;
  };

  if (mode == shm_open_mode::create) {
    if (size_ == 0) {
      last_os_error_ = EINVAL;
      return false;
    }
    fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd_ != -1) {
      created_this_open_ = true;
    }
  } else if (mode == shm_open_mode::open) {
    fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
  } else if (mode == shm_open_mode::open_create) {
    fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
    if (fd_ == -1) {
      if (size_ == 0) {
        last_os_error_ = EINVAL;
        return false;
      }
      fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
      if (fd_ != -1) {
        created_this_open_ = true;
      } else if (errno == EEXIST) {
        fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
      }
    }
  } else if (mode == shm_open_mode::read) {
    fd_ = shm_open(name_.c_str(), O_RDONLY, 0666);
  }

  if (fd_ == -1) {
    last_os_error_ = errno;
    return false;
  }

  if (created_this_open_) {
    if (ftruncate(fd_, size_) == -1) {
      return close_and_fail(errno);
    }
  } else {
    struct stat st {};
    if (fstat(fd_, &st) == -1) {
      return close_and_fail(errno);
    }
    const size_t existing_size = static_cast<size_t>(st.st_size);
    if (size_ == 0) {
      size_ = existing_size;
    } else if (existing_size < size_) {
      return close_and_fail(EINVAL);
    }
  }

  if (size_ == 0) {
    return close_and_fail(EINVAL);
  }

  int port = PROT_READ | (mode == shm_open_mode::read ? 0 : PROT_WRITE);
  addr_ = mmap(nullptr, size_, port, MAP_SHARED, fd_, 0);

  if (addr_ == MAP_FAILED) {
    addr_ = nullptr;
    return close_and_fail(errno);
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
  created_this_open_ = false;
}

void shm::unlink(const std::string& name) {
  // Best-effort unlink; callers should not rely on success (e.g., on Windows unlink is no-op).
  // We intentionally ignore ENOENT (already removed race) but log via last_error for diagnostics.
  shm_unlink(name.c_str());
}

}  // namespace shm
}  // namespace xproc
