#pragma once

#include <xproc/shm/shm_open_mode.hpp>
#include <string>
#include <cstddef>

namespace xproc {
namespace shm {

class shm {
public:
    explicit shm() = default;
    ~shm() {
        detach();
    }

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

private:
    void* addr_{nullptr};
    size_t size_{0};
    int fd_{-1};
    std::string name_;
};

} // namespace memory
} // namespace xproc
