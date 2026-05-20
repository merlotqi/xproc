include(FetchContent)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.4
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# CTest labels for xproc_run_benchmarks (also works when XPROC_BUILD_TESTS is OFF).
include(CTest)

function(xproc_add_benchmark short_name source_file)
  set(tgt xproc_bench_${short_name})
  add_executable(${tgt} ${source_file})
  target_link_libraries(${tgt} PRIVATE xproc benchmark::benchmark_main)
  set_target_properties(${tgt} PROPERTIES FOLDER "Benchmarks")
  add_test(NAME xproc.bench.${short_name} COMMAND ${tgt})
  set_tests_properties(xproc.bench.${short_name} PROPERTIES 
    LABELS "xproc_benchmark"
    TIMEOUT 120
  )
endfunction()

xproc_add_benchmark(ipc ipc_benchmark.cpp)
xproc_add_benchmark(ipc_observer ipc_observer_benchmark.cpp)
xproc_add_benchmark(ipc_poll_copy ipc_poll_copy_benchmark.cpp)
xproc_add_benchmark(ipc_cross_framework ipc_cross_framework_benchmark.cpp)
xproc_add_benchmark(ringbuffer ringbuffer_benchmark.cpp)
xproc_add_benchmark(shm_layout shm_layout_benchmark.cpp)
add_executable(xproc_bench_sync sync_benchmark.cpp)
target_link_libraries(xproc_bench_sync PRIVATE xproc benchmark::benchmark)
set_target_properties(xproc_bench_sync PROPERTIES FOLDER "Benchmarks")
add_test(NAME xproc.bench.sync COMMAND xproc_bench_sync)
set_tests_properties(xproc.bench.sync PROPERTIES LABELS "xproc_benchmark")

if(CMAKE_CONFIGURATION_TYPES)
  set(_xproc_ctest_config_arg "-C" "$<CONFIG>")
elseif(CMAKE_BUILD_TYPE)
  set(_xproc_ctest_config_arg "-C" "${CMAKE_BUILD_TYPE}")
else()
  set(_xproc_ctest_config_arg "")
endif()

add_custom_target(
  xproc_run_benchmarks
  COMMAND ${CMAKE_CTEST_COMMAND} ${_xproc_ctest_config_arg} -L xproc_benchmark --output-on-failure
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  USES_TERMINAL
  COMMENT "Running all xproc benchmarks via CTest (label: xproc_benchmark)"
)
set_target_properties(xproc_run_benchmarks PROPERTIES FOLDER "Benchmarks")
add_dependencies(
  xproc_run_benchmarks
  xproc_bench_ipc
  xproc_bench_ipc_cross_framework
  xproc_bench_ipc_observer
  xproc_bench_ipc_poll_copy
  xproc_bench_ringbuffer
  xproc_bench_shm_layout
  xproc_bench_sync
)

option(XPROC_BENCH_QT "Enable Qt shared-memory IPC benchmarks" OFF)
if(XPROC_BENCH_QT)
  find_package(Qt6 COMPONENTS Core QUIET)
  if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Core)
  endif()
  target_link_libraries(
    xproc_bench_ipc_cross_framework PRIVATE
    $<TARGET_NAME_IF_EXISTS:Qt6::Core>
    $<TARGET_NAME_IF_EXISTS:Qt5::Core>
  )
  target_compile_definitions(xproc_bench_ipc_cross_framework PRIVATE XPROC_BENCH_QT)
endif()

option(XPROC_BENCH_POCO "Enable Poco shared-memory IPC benchmarks" OFF)
if(XPROC_BENCH_POCO)
  find_package(Poco REQUIRED COMPONENTS Foundation)
  target_link_libraries(xproc_bench_ipc_cross_framework PRIVATE Poco::Foundation)
  target_compile_definitions(xproc_bench_ipc_cross_framework PRIVATE XPROC_BENCH_POCO)
endif()

set(XPROC_IPC_BENCHMARK_JSON "${CMAKE_BINARY_DIR}/ipc-cross-framework.json")
set(XPROC_IPC_BENCHMARK_RST "${CMAKE_SOURCE_DIR}/docs/_generated/ipc_benchmark_table.rst")
set(XPROC_SYNC_BENCHMARK_JSON "${CMAKE_BINARY_DIR}/sync-win32-wait.json")
set(XPROC_SYNC_BENCHMARK_RST "${CMAKE_SOURCE_DIR}/docs/_generated/sync_win32_wait_table.rst")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(
  xproc_generate_ipc_benchmark_docs
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/docs/_generated"
  COMMAND $<TARGET_FILE:xproc_bench_ipc_cross_framework>
          --benchmark_format=json
          --benchmark_out=${XPROC_IPC_BENCHMARK_JSON}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/generate_ipc_benchmark_table.py
          --input ${XPROC_IPC_BENCHMARK_JSON}
          --output ${XPROC_IPC_BENCHMARK_RST}
  DEPENDS xproc_bench_ipc_cross_framework
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  USES_TERMINAL
  COMMENT "Generate IPC benchmark JSON and RST table"
)

if(WIN32)
  add_custom_target(
    xproc_generate_sync_benchmark_docs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/docs/_generated"
    COMMAND $<TARGET_FILE:xproc_bench_sync>
            --benchmark_format=json
            --benchmark_out=${XPROC_SYNC_BENCHMARK_JSON}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/generate_sync_wait_benchmark_table.py
            --input ${XPROC_SYNC_BENCHMARK_JSON}
            --output ${XPROC_SYNC_BENCHMARK_RST}
    DEPENDS xproc_bench_sync
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
    COMMENT "Generate sync benchmark Win32 wait-counter JSON and RST table"
  )
endif()
