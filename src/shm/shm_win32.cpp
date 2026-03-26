#include <xproc/platform/platform.hpp>
#include <xproc/shm/shm.hpp>

#if !defined(_WIN32)
#error shm_win32.cpp is Windows-only
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

namespace xproc {
namespace shm {
namespace {

std::uint64_t fnv1a64(const std::string &path) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t h = kOffset;
  for (unsigned char c : path) {
    h ^= static_cast<std::uint64_t>(c);
    h *= kPrime;
  }
  return h;
}

// Local\xproc_<fnv_hex>_<sanitized_path_suffix> — FNV reduces collisions vs. sanitize-only names.
std::string mapping_name_from_path(const std::string &path) {
  std::string n = "Local\\xproc_";
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fnv1a64(path)));
  n += hex;
  n.push_back('_');
  constexpr std::size_t kMaxSuffix = 160;
  std::size_t taken = 0;
  for (unsigned char c : path) {
    if (taken >= kMaxSuffix) {
      break;
    }
    if (std::isalnum(c) != 0) {
      n.push_back(static_cast<char>(c));
    } else {
      n.push_back('_');
    }
    ++taken;
  }
  return n;
}

// Map the full section; require VirtualQuery RegionSize >= opts.shm_size (caller may have rounded
// CreateFileMapping size up — extra tail bytes are unused; Linux mmap uses exact length).
bool map_and_verify_size(HANDLE h, DWORD map_access, std::size_t expected_bytes, void **out_addr) {
  void *p = ::MapViewOfFile(h, map_access, 0, 0, 0);
  if (!p) {
    return false;
  }
  MEMORY_BASIC_INFORMATION mbi{};
  if (!::VirtualQuery(p, &mbi, sizeof(mbi))) {
    ::UnmapViewOfFile(p);
    return false;
  }
  if (mbi.RegionSize < expected_bytes) {
    ::UnmapViewOfFile(p);
    ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return false;
  }
  *out_addr = p;
  return true;
}

}  // namespace

shm::shm(shm &&other) noexcept { *this = std::move(other); }

shm &shm::operator=(shm &&other) noexcept {
  if (this != &other) {
    detach();

    addr_ = other.addr_;
    size_ = other.size_;
    mapping_ = other.mapping_;
    last_os_error_ = other.last_os_error_;
    name_ = std::move(other.name_);

    other.addr_ = nullptr;
    other.size_ = 0;
    other.mapping_ = nullptr;
    other.last_os_error_ = 0;
  }
  return *this;
}

bool shm::open(const std::string &name, size_t size, shm_open_mode mode) {
  last_os_error_ = 0;
  name_ = mapping_name_from_path(name);
  size_ = size;

  const DWORD map_access = (mode == shm_open_mode::read) ? FILE_MAP_READ : FILE_MAP_WRITE | FILE_MAP_READ;

  HANDLE h = nullptr;
  if (mode == shm_open_mode::open || mode == shm_open_mode::read) {
    h = ::OpenFileMappingA(map_access, FALSE, name_.c_str());
    if (!h) {
      last_os_error_ = static_cast<int>(::GetLastError());
      return false;
    }
  } else if (mode == shm_open_mode::open_create) {
    h = ::OpenFileMappingA(map_access, FALSE, name_.c_str());
    if (!h) {
      const DWORD hi = static_cast<DWORD>((size_ >> 32) & 0xffffffffu);
      const DWORD lo = static_cast<DWORD>(size_ & 0xffffffffu);
      h = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, hi, lo, name_.c_str());
      if (!h) {
        last_os_error_ = static_cast<int>(::GetLastError());
        return false;
      }
    }
  } else if (mode == shm_open_mode::create) {
    const DWORD hi = static_cast<DWORD>((size_ >> 32) & 0xffffffffu);
    const DWORD lo = static_cast<DWORD>(size_ & 0xffffffffu);
    h = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, hi, lo, name_.c_str());
    if (!h) {
      last_os_error_ = static_cast<int>(::GetLastError());
      return false;
    }
  } else {
    return false;
  }

  void *p = nullptr;
  if (!map_and_verify_size(h, map_access, size_, &p)) {
    last_os_error_ = static_cast<int>(::GetLastError());
    ::CloseHandle(h);
    return false;
  }

  mapping_ = h;
  addr_ = p;
  return true;
}

void shm::detach() {
  if (addr_) {
    ::UnmapViewOfFile(addr_);
    addr_ = nullptr;
  }
  if (mapping_) {
    ::CloseHandle(mapping_);
    mapping_ = nullptr;
  }
  size_ = 0;
}

void shm::unlink(const std::string &name) {
  (void)name;
  // No POSIX-style shm_unlink on Windows; last CloseHandle on the mapping object releases the name.
}

}  // namespace shm
}  // namespace xproc
