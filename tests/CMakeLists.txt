include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# Register a test executable that links gtest_main and discovers individual TEST() cases.
function(xproc_gtest_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE xproc GTest::gtest_main)
    set_target_properties(${name} PROPERTIES FOLDER "Tests")
    gtest_discover_tests(${name})
    set_property(GLOBAL APPEND PROPERTY XPROC_ALL_TEST_TARGETS ${name})
endfunction()

# Portable tests (all supported platforms).
set(XPROC_GTEST_TEST_FILES_COMMON
    api_surface_test.cpp
    ringbuffer_spsc_test.cpp
    layout_validate_test.cpp
    ipc_observer_attach_test.cpp
    protocol_codec_test.cpp
    socket_transport_test.cpp
)

# Linux-only (fork, pipe, POSIX paths). Not built on Windows or other OSes.
set(XPROC_GTEST_TEST_FILES_LINUX
    ipc_integration_test.cpp
    ringbuffer_full_ring_test.cpp
)

set(TEST_FILES ${XPROC_GTEST_TEST_FILES_COMMON})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND TEST_FILES ${XPROC_GTEST_TEST_FILES_LINUX})
endif()

foreach(TEST_FILE ${TEST_FILES})
    string(REPLACE ".cpp" "" TEST_NAME ${TEST_FILE})
    xproc_gtest_executable(xproc_${TEST_NAME} ${TEST_FILE})
endforeach()

if(WIN32)
    add_executable(xproc_win32_wait_shm_tests win32_wait_shm_test.cpp)
    target_link_libraries(xproc_win32_wait_shm_tests PRIVATE xproc GTest::gtest)
    set_target_properties(xproc_win32_wait_shm_tests PROPERTIES FOLDER "Tests")
    gtest_discover_tests(xproc_win32_wait_shm_tests)
    set_property(GLOBAL APPEND PROPERTY XPROC_ALL_TEST_TARGETS xproc_win32_wait_shm_tests)
endif()

if(TARGET xproc_c)
    add_executable(xproc_capi_smoke_tests capi_smoke_test.cpp)
    target_link_libraries(xproc_capi_smoke_tests PRIVATE xproc_c GTest::gtest_main)
    set_target_properties(xproc_capi_smoke_tests PROPERTIES FOLDER "Tests")
    gtest_discover_tests(xproc_capi_smoke_tests)
    set_property(GLOBAL APPEND PROPERTY XPROC_ALL_TEST_TARGETS xproc_capi_smoke_tests)
endif()

if(XPROC_BUILD_TESTS AND (XPROC_WITH_NLOHMANN_JSON OR XPROC_WITH_PROTOBUF))
    set(_xproc_opt_serde_src optional_serde_test.cpp)
    set(_xproc_opt_serde_gen "")
    if(XPROC_WITH_PROTOBUF)
        find_package(Protobuf REQUIRED)
        protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS "${CMAKE_SOURCE_DIR}/proto/test_point.proto")
        set(_xproc_opt_serde_gen ${PROTO_SRCS} ${PROTO_HDRS})
    endif()
    add_executable(xproc_optional_serde_tests ${_xproc_opt_serde_src} ${_xproc_opt_serde_gen})
    set_target_properties(xproc_optional_serde_tests PROPERTIES FOLDER "Tests")
    target_link_libraries(xproc_optional_serde_tests PRIVATE xproc GTest::gtest_main)
    if(XPROC_WITH_PROTOBUF)
        target_include_directories(xproc_optional_serde_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    gtest_discover_tests(xproc_optional_serde_tests)
    set_property(GLOBAL APPEND PROPERTY XPROC_ALL_TEST_TARGETS xproc_optional_serde_tests)
endif()

get_property(XPROC_ALL_TEST_TARGETS GLOBAL PROPERTY XPROC_ALL_TEST_TARGETS)

# Multi-config generators (Visual Studio): ctest must get -C <Config> or tests show "Not Run".
if(CMAKE_CONFIGURATION_TYPES)
    set(_xproc_run_tests_ctest_cfg "-C" "$<CONFIG>")
elseif(CMAKE_BUILD_TYPE)
    set(_xproc_run_tests_ctest_cfg "-C" "${CMAKE_BUILD_TYPE}")
else()
    set(_xproc_run_tests_ctest_cfg "-C" "Debug")
endif()

add_custom_target(xproc_run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} ${_xproc_run_tests_ctest_cfg} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
    VERBATIM
    COMMENT "Running all xproc tests"
)

if(XPROC_ALL_TEST_TARGETS)
    add_dependencies(xproc_run_tests ${XPROC_ALL_TEST_TARGETS})
endif()
