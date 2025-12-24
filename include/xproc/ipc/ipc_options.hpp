#pragma once

#include <string>
#include <cstdint>

namespace xproc {
namespace ipc {

enum class channel_type {
    fixed,
    variable
};

struct transport_options {
    std::string path;
    size_t shm_size;
    uint32_t item_size = 0;
    uint32_t data_align = 0;
    bool create_if_missing = true;
    channel_type type = channel_type::fixed;
};

} // namespace ipc
} // namespace xproc