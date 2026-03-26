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
  add_test(NAME xproc.bench.${short_name} COMMAND ${tgt})
  set_tests_properties(xproc.bench.${short_name} PROPERTIES LABELS "xproc_benchmark")
endfunction()

xproc_add_benchmark(ipc ipc_benchmark.cpp)
xproc_add_benchmark(ipc_observer ipc_observer_benchmark.cpp)
xproc_add_benchmark(ipc_poll_copy ipc_poll_copy_benchmark.cpp)
xproc_add_benchmark(ringbuffer ringbuffer_benchmark.cpp)
xproc_add_benchmark(shm_layout shm_layout_benchmark.cpp)
xproc_add_benchmark(sync sync_benchmark.cpp)

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
add_dependencies(
  xproc_run_benchmarks
  xproc_bench_ipc
  xproc_bench_ipc_observer
  xproc_bench_ipc_poll_copy
  xproc_bench_ringbuffer
  xproc_bench_shm_layout
  xproc_bench_sync
)
