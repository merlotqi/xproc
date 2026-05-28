#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <xproc/core/shm.hpp>

#if defined(_WIN32)
#error shm_posix.cpp must not be built on Windows
#endif

namespace xproc::core {
namespace {

// FNV-1a 64-bit hash — same algorithm used by the Win32 shm backend.
std::uint64_t fnv1a64(const std::string& s) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t h = kOffset;
  for (unsigned char c : s) {
    h ^= static_cast<std::uint64_t>(c);
    h *= kPrime;
  }
  return h;
}

// macOS shm_open enforces PSHMNAMLEN (31 chars after the leading '/').
// Build a deterministic short name: /xproc_<16-hex>_<7-char-suffix>
// Total = 1 + 6 + 1 + 16 + 1 + 7 = 32 chars including '/', fits the limit.
// But macOS PSHMNAMLEN = 31 TOTAL including '/', so we cap at 31.
// Linux shm_open allows 255 chars so this path is only needed on Apple, but
// applying it unconditionally keeps the code simple and the name stable across
// platforms (a consumer on Linux can open a segment created on macOS via NFS or
// similar shared /dev/shm, if that were ever relevant).
std::string shm_name_for_kernel(const std::string& user_name) {
#ifdef __APPLE__
  constexpr std::size_t kMaxKernelNameLen = 31;  // PSHMNAMLEN on Darwin
  if (user_name.size() <= kMaxKernelNameLen) {
    return user_name;
  }
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fnv1a64(user_name)));
  std::string n = "/xproc_";
  n += hex;
  n.push_back('_');
  // Append up to 7 alphanumeric chars from the tail of user_name for debuggability.
  std::size_t taken = 0;
  for (auto it = user_name.rbegin(); it != user_name.rend() && taken < 7; ++it) {
    unsigned char c = static_cast<unsigned char>(*it);
    if (c == '/') break;
    if (std::isalnum(c) != 0) {
      n.push_back(static_cast<char>(c));
      ++taken;
    }
  }
  // Pad with '0' if suffix < 7 chars so name length is always 31.
  while (n.size() < 31) {
    n.push_back('0');
  }
  return n;
#else
  return user_name;
#endif
}

}  // namespace

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

  // macOS shm_open enforces PSHMNAMLEN (31 chars).  Hash long names to a
  // deterministic short form so callers are not burdened with platform limits.
  const std::string kname = shm_name_for_kernel(name_);

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
    fd_ = shm_open(kname.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd_ != -1) {
      created_this_open_ = true;
    }
  } else if (mode == shm_open_mode::open) {
    fd_ = shm_open(kname.c_str(), O_RDWR, 0666);
  } else if (mode == shm_open_mode::open_create) {
    fd_ = shm_open(kname.c_str(), O_RDWR, 0666);
    if (fd_ == -1) {
      if (size_ == 0) {
        last_os_error_ = EINVAL;
        return false;
      }
      fd_ = shm_open(kname.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
      if (fd_ != -1) {
        created_this_open_ = true;
      } else if (errno == EEXIST) {
        fd_ = shm_open(kname.c_str(), O_RDWR, 0666);
      }
    }
  } else if (mode == shm_open_mode::read) {
    fd_ = shm_open(kname.c_str(), O_RDONLY, 0666);
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
  shm_unlink(shm_name_for_kernel(name).c_str());
}

}  // namespace xproc::core
