#include "xproc/shm/shm_layout.hpp"
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <xproc/platform/platform.hpp>
#include <xproc/shm/shm_layout_manager.hpp>

namespace xproc {
namespace shm {

shm_control_block *shm_layout_manager::format(shm &sm, size_t capacity, bool is_creator)
{
    if (sm.is_attached())
    {
        return nullptr;
    }

    auto *header = static_cast<shm_control_block *>(sm.addr());
    if (is_creator)
    {
        _init_header(header, capacity);
    }
    else
    {
        if (!validate(header, capacity))
        {
            return nullptr;
        }
        header->attach_count.fetch_add(1, std::memory_order_relaxed);
    }
    return header;
}

bool shm_layout_manager::validate(shm_control_block *header, size_t expected_capacity)
{
    if (header->magic != EXPECTED_MAGIC)
    {
        return false;
    }

    int timeout_limit = 1'000'000;
    while (!header->is_ready.load(std::memory_order_acquire))
    {
        XPROC_CPU_PAUSE();
        if (--timeout_limit <= 0)
            return false;
    }

    if (header->version_major != VERSION_MAJOR)
    {
        return false;
    }

    if (header->data_capacity < expected_capacity)
    {
        return false;
    }

    return true;
}

void shm_layout_manager::_init_header(shm_control_block *header, size_t capacity)
{
    header->magic = EXPECTED_MAGIC;
    header->version_major = VERSION_MAJOR;
    header->version_minor = VERSION_MINOR;
    header->header_size = sizeof(shm_control_block);
    header->layout_type = 1;

    header->rb_meta.write_pos.store(0, std::memory_order_relaxed);
    header->rb_meta.read_pos.store(0, std::memory_order_relaxed);

    header->data_capacity = capacity;
    header->data_alignment = 8;

    header->producer_pid.store(getpid(), std::memory_order_relaxed);
    header->attach_count.store(1, std::memory_order_relaxed);

    header->is_ready.store(true, std::memory_order_release);
}

}// namespace shm
}// namespace xproc
