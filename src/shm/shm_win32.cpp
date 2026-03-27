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
std::mutex g_win32_shm_mutex;
std::unordered_map<std::string, HANDLE> g_canonical_shm_handle;

struct ProcessViewEntry {
  void* addr{nullptr};
  HANDLE section_handle{nullptr};
  int refcount{0};
};

// One MapViewOfFile per (object name, map access, size) in this process so producer/consumer share a VA.
std::unordered_map<std::string, ProcessViewEntry> g_process_views;

std::uint64_t fnv1a64(const std::string& path) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t h = kOffset;
  for (unsigned char c : path) {
    h ^= static_cast<std::uint64_t>(c);
    h *= kPrime;
  }
  return h;
}

// <namespace>\xproc_<fnv_hex>_<sanitized_path_suffix>. FNV-1a lowers collision risk but different logical paths
// can still map to the same object name; callers should use unique path strings (see docs/design.rst).
std::string mapping_name_from_path(const std::string& path, const std::string& ns) {
  if (ns != "Local" && ns != "Global") {
    return {};
  }
  std::string n = ns + "\\xproc_";
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

std::string view_registry_key(const std::string& internal_name, DWORD map_access, std::size_t size_bytes) {
  return internal_name + "|" + std::to_string(static_cast<unsigned long long>(map_access)) + "|" +
         std::to_string(static_cast<unsigned long long>(size_bytes));
}

// Map the full section; require VirtualQuery RegionSize >= opts.shm_size (caller may have rounded
// CreateFileMapping size up — extra tail bytes are unused; Linux mmap uses exact length).
bool map_and_verify_size(HANDLE h, DWORD map_access, std::size_t expected_bytes, void** out_addr) {
  void* p = ::MapViewOfFile(h, map_access, 0, 0, 0);
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

shm::shm(shm&& other) noexcept { *this = std::move(other); }

shm& shm::operator=(shm&& other) noexcept {
  if (this != &other) {
    detach();

    addr_ = other.addr_;
    size_ = other.size_;
    mapping_ = other.mapping_;
    last_os_error_ = other.last_os_error_;
    name_ = std::move(other.name_);
    win32_view_key_ = std::move(other.win32_view_key_);

    other.addr_ = nullptr;
    other.size_ = 0;
    other.mapping_ = nullptr;
    other.last_os_error_ = 0;
    other.win32_view_key_.clear();
  }
  return *this;
}

bool shm::open(const std::string& name, size_t size, shm_open_mode mode, const std::string& win32_object_namespace) {
  last_os_error_ = 0;
  name_ = mapping_name_from_path(name, win32_object_namespace);
  if (name_.empty()) {
    last_os_error_ = static_cast<int>(ERROR_INVALID_PARAMETER);
    return false;
  }
  size_ = size;

  const DWORD map_access = (mode == shm_open_mode::read) ? FILE_MAP_READ : FILE_MAP_WRITE | FILE_MAP_READ;
  const std::string vkey = view_registry_key(name_, map_access, size_);

  HANDLE h = nullptr;
  bool register_canonical = false;

  if (mode == shm_open_mode::open || mode == shm_open_mode::read) {
    HANDLE dup_src = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_win32_shm_mutex);
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

  {
    std::lock_guard<std::mutex> lock(g_win32_shm_mutex);
    const auto vit = g_process_views.find(vkey);
    if (vit != g_process_views.end()) {
      ::CloseHandle(h);
      addr_ = vit->second.addr;
      mapping_ = nullptr;
      win32_view_key_ = vkey;
      vit->second.refcount += 1;
      if (register_canonical && g_canonical_shm_handle.find(name_) == g_canonical_shm_handle.end()) {
        g_canonical_shm_handle[name_] = vit->second.section_handle;
      }
      return true;
    }
  }

  void* p = nullptr;
  if (!map_and_verify_size(h, map_access, size_, &p)) {
    last_os_error_ = static_cast<int>(::GetLastError());
    ::CloseHandle(h);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(g_win32_shm_mutex);
    const auto vit2 = g_process_views.find(vkey);
    if (vit2 != g_process_views.end()) {
      ::UnmapViewOfFile(p);
      ::CloseHandle(h);
      addr_ = vit2->second.addr;
      mapping_ = nullptr;
      win32_view_key_ = vkey;
      vit2->second.refcount += 1;
      if (register_canonical && g_canonical_shm_handle.find(name_) == g_canonical_shm_handle.end()) {
        g_canonical_shm_handle[name_] = vit2->second.section_handle;
      }
      return true;
    }

    ProcessViewEntry e;
    e.addr = p;
    e.section_handle = h;
    e.refcount = 1;
    g_process_views.emplace(vkey, std::move(e));

    if (register_canonical && g_canonical_shm_handle.find(name_) == g_canonical_shm_handle.end()) {
      g_canonical_shm_handle[name_] = h;
    }
  }

  addr_ = p;
  mapping_ = h;
  win32_view_key_ = vkey;
  return true;
}

void shm::detach() {
  const std::string name_copy = name_;
  const std::string vkey_copy = win32_view_key_;

  if (!vkey_copy.empty()) {
    std::lock_guard<std::mutex> lock(g_win32_shm_mutex);
    const auto it = g_process_views.find(vkey_copy);
    if (it != g_process_views.end()) {
      ProcessViewEntry& e = it->second;
      e.refcount -= 1;
      if (e.refcount <= 0) {
        void* const ap = e.addr;
        HANDLE const eh = e.section_handle;
        g_process_views.erase(it);
        const auto cit = g_canonical_shm_handle.find(name_copy);
        if (cit != g_canonical_shm_handle.end() && cit->second == eh) {
          g_canonical_shm_handle.erase(cit);
        }
        ::UnmapViewOfFile(ap);
        ::CloseHandle(eh);
      }
    }
    addr_ = nullptr;
    mapping_ = nullptr;
    win32_view_key_.clear();
    name_.clear();
    size_ = 0;
    return;
  }

  if (addr_) {
    ::UnmapViewOfFile(addr_);
    addr_ = nullptr;
  }
  if (mapping_) {
    HANDLE to_close = static_cast<HANDLE>(mapping_);
    {
      std::lock_guard<std::mutex> lock(g_win32_shm_mutex);
      const auto cit = g_canonical_shm_handle.find(name_copy);
      if (cit != g_canonical_shm_handle.end() && cit->second == to_close) {
        g_canonical_shm_handle.erase(cit);
      }
    }
    ::CloseHandle(to_close);
    mapping_ = nullptr;
  }
  name_.clear();
  size_ = 0;
}

void shm::unlink(const std::string& name) {
  (void)name;
  // No POSIX-style shm_unlink on Windows; last CloseHandle on the mapping object releases the name.
}

}  // namespace shm
}  // namespace xproc
