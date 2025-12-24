#pragma once

#include <xproc/shm/shm.hpp>
#include <xproc/shm/shm_layout.hpp>

namespace xproc {
namespace shm {

class shm_layout_manager
{
public:
    static constexpr uint32_t EXPECTED_MAGIC = 0x58505243;
    static constexpr uint16_t VERSION_MAJOR = 0;
    static constexpr uint32_t VERSION_MINOR = 1;

    static shm_control_block *format(shm &sm, size_t capacity, bool is_creator);
    static bool validate(shm_control_block *header, size_t expected_capacity);

private:
    static void _init_header(shm_control_block *header, size_t capacity);
};

}// namespace shm
}// namespace xproc
