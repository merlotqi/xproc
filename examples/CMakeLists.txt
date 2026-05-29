add_subdirectory(extern/taskflow)

set(XPROC_EXAMPLE_FILES
    mpmc_inprocess_bridge_demo.cpp
    mpmc_worker_pool_demo.cpp
    mpsc_fan_in_demo.cpp
    mpsc_log_hub_demo.cpp
    spmc_config_broadcast_demo.cpp
    spmc_fan_out_demo.cpp
    ping_pong.cpp
    fixed_channel_inprocess.cpp
    varlen_channel_inprocess.cpp
    observer_peek_demo.cpp
    codec_roundtrip_demo.cpp
    socket_varlen_reconnect_demo.cpp
    runtime_dispatch_demo.cpp
    parent_child_counter_monitor.cpp
    parent_child_struct_monitor.cpp
    parent_child_varlen_monitor.cpp
    node_cpp_child_struct_writer.cpp
    handshake_launcher_demo.cpp
    cpp_python_handshake_progress.cpp
)

foreach(example_file ${XPROC_EXAMPLE_FILES})
    get_filename_component(example_name "${example_file}" NAME_WE)
    add_executable(xproc_${example_name} "${example_file}")
    set_target_properties(xproc_${example_name} PROPERTIES FOLDER "Examples")
    target_link_libraries(xproc_${example_name} PRIVATE xproc)
endforeach()

target_compile_definitions(xproc_cpp_python_handshake_progress PRIVATE
    XPROC_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    XPROC_BINARY_ROOT="${CMAKE_BINARY_DIR}"
)

add_executable(xproc_ipc_taskflow_runtime_demo ipc_taskflow_runtime_demo.cpp)
set_target_properties(xproc_ipc_taskflow_runtime_demo PROPERTIES FOLDER "Examples")
target_link_libraries(xproc_ipc_taskflow_runtime_demo PRIVATE xproc TaskFlow::TaskFlow)

add_executable(xproc_ipc_taskflow_pipeline_demo ipc_taskflow_pipeline_demo.cpp)
set_target_properties(xproc_ipc_taskflow_pipeline_demo PROPERTIES FOLDER "Examples")
target_link_libraries(xproc_ipc_taskflow_pipeline_demo PRIVATE xproc TaskFlow::TaskFlow)
