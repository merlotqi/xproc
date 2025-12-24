#pragma once

#include <atomic>
#include <xproc/platform/platform.hpp>

namespace xproc {
namespace shm {

struct shm_meta
{
    XPROC_ALIGNAS_CACHE_LINE std::atomic<uint64_t> write_pos{0};
    uint8_t padding1[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<uint64_t>)];

    XPROC_ALIGNAS_CACHE_LINE std::atomic<uint64_t> read_pos{0};
    uint8_t padding2[XPROC_CACHE_LINE_SIZE - sizeof(std::atomic<uint64_t>)];
};

struct XPROC_ALIGNAS_CACHE_LINE shm_control_block
{
    uint32_t magic;
    uint16_t version_major;
    uint32_t version_minor;
    uint32_t header_size;
    uint32_t layout_type;// 0: fixed, 1: variable length

    std::atomic<uint32_t> attach_count{0};
    std::atomic<bool> is_ready{false};
    std::atomic<int32_t> producer_pid{0};

    uint8_t padding_identity[XPROC_CACHE_LINE_SIZE - 28];

    shm_meta rb_meta;

    uint64_t data_capacity;
    uint32_t data_alignment;

    uint64_t reserved[4];
};

}// namespace shm
}// namespace xproc
