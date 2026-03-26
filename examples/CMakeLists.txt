set(XPROC_EXAMPLE_FILES
    ping_pong.cpp
    fixed_channel_inprocess.cpp
    varlen_channel_inprocess.cpp
    observer_peek_demo.cpp
    codec_roundtrip_demo.cpp
    runtime_dispatch_demo.cpp
    parent_child_counter_monitor.cpp
    parent_child_struct_monitor.cpp
)

foreach(example_file ${XPROC_EXAMPLE_FILES})
    get_filename_component(example_name "${example_file}" NAME_WE)
    add_executable(xproc_${example_name} "${example_file}")
    target_link_libraries(xproc_${example_name} PRIVATE xproc)
endforeach()
