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
#include <mutex>
#include <string>
#include <unordered_map>

namespace xproc {
namespace shm {
namespace {

// Same-process second opens: OpenFileMapping can block or misbehave while the creator still holds the
// section handle; DuplicateHandle from the creator's mapping object is reliable (see in-process IPC tests).
std::mutex g_canonical_shm_mutex;
std::unordered_map<std::string, HANDLE> g_canonical_shm_handle;

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

// Local\xproc_<fnv_hex>_<sanitized_path_suffix>. FNV-1a lowers collision risk but different logical paths
// can still map to the same object name; callers should use unique path strings (see docs/design.md).
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
  bool register_canonical = false;

  if (mode == shm_open_mode::open || mode == shm_open_mode::read) {
    HANDLE dup_src = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_canonical_shm_mutex);
      const auto it = g_canonical_shm_handle.find(name_);
      if (it != g_canonical_shm_handle.end()) {
        dup_src = it->second;
      }
    }
    if (dup_src != nullptr) {
      if (!::DuplicateHandle(GetCurrentProcess(), dup_src, GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        last_os_error_ = static_cast<int>(::GetLastError());
        return false;
      }
    } else {
      h = ::OpenFileMappingA(map_access, FALSE, name_.c_str());
      if (!h) {
        last_os_error_ = static_cast<int>(::GetLastError());
        return false;
      }
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
    // Always register for open_create so a same-process consumer can DuplicateHandle while this handle
    // remains open (OpenFileMapping-only second opens can block or wedge against the first handle).
    register_canonical = true;
  } else if (mode == shm_open_mode::create) {
    const DWORD hi = static_cast<DWORD>((size_ >> 32) & 0xffffffffu);
    const DWORD lo = static_cast<DWORD>(size_ & 0xffffffffu);
    h = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, hi, lo, name_.c_str());
    if (!h) {
      last_os_error_ = static_cast<int>(::GetLastError());
      return false;
    }
    register_canonical = true;
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
  if (register_canonical) {
    std::lock_guard<std::mutex> lock(g_canonical_shm_mutex);
    // Keep the first handle for DuplicateHandle sources; do not overwrite with a second Open handle
    // for the same mapping name (would strand waiters on a stale handle after the creator detaches).
    if (g_canonical_shm_handle.find(name_) == g_canonical_shm_handle.end()) {
      g_canonical_shm_handle[name_] = static_cast<HANDLE>(mapping_);
    }
  }
  return true;
}

void shm::detach() {
  if (addr_) {
    ::UnmapViewOfFile(addr_);
    addr_ = nullptr;
  }
  if (mapping_) {
    HANDLE to_close = static_cast<HANDLE>(mapping_);
    {
      std::lock_guard<std::mutex> lock(g_canonical_shm_mutex);
      const auto it = g_canonical_shm_handle.find(name_);
      if (it != g_canonical_shm_handle.end() && it->second == to_close) {
        g_canonical_shm_handle.erase(it);
      }
    }
    ::CloseHandle(to_close);
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
