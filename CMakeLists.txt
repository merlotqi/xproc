cmake_minimum_required(VERSION 3.14...3.24)
project(xproc VERSION 0.2.0 LANGUAGES CXX)

# Visual Studio generator: default to x64 when -A is omitted, avoiding winnt.h C1189 "No Target Architecture".
if(WIN32 AND MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    if(NOT CMAKE_GENERATOR_PLATFORM)
        set(CMAKE_GENERATOR_PLATFORM "x64" CACHE STRING "Visual Studio platform selection")
    endif()
endif()

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
    message(FATAL_ERROR "xproc supports Linux and Windows only (found ${CMAKE_SYSTEM_NAME}); see docs/design.md")
endif()

if(WIN32)
    message(STATUS "xproc: Windows build: atomic_wait uses polling (WaitOnAddress does not pair across separate MapViewOfFile views).")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_library(LIB_RT rt)
    find_package(Threads REQUIRED)
endif()

add_subdirectory(src)

option(XPROC_BUILD_CAPI "Build xproc_c C ABI wrapper" ON)
if(XPROC_BUILD_CAPI)
    add_subdirectory(capi)
endif()

option(XPROC_BUILD_TESTS "Build unit tests" ON)
if(XPROC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(XPROC_BUILD_EXAMPLES "Build examples" ON)
if(XPROC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

option(XPROC_BUILD_BENCHMARKS "Build benchmarks with Google Benchmark" ON)
if(XPROC_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

option(XPROC_BUILD_NODE "Build JavaScript binding" ON)
set(NODE_INCLUDE_DIR "" CACHE PATH
    "Optional: override directory containing node_api.h. Empty = use node_modules/node-api-headers from npm ci in node/.")
if(XPROC_BUILD_NODE)
    add_subdirectory(node)
endif()

set(XPROC_EXPORT_WITH_NLOHMANN_JSON 0)
if(XPROC_WITH_NLOHMANN_JSON)
    set(XPROC_EXPORT_WITH_NLOHMANN_JSON 1)
endif()
set(XPROC_EXPORT_WITH_PROTOBUF 0)
if(XPROC_WITH_PROTOBUF)
    set(XPROC_EXPORT_WITH_PROTOBUF 1)
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xprocConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/xprocConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xproc
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xprocConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

set(prefix "${CMAKE_INSTALL_PREFIX}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xproc.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/xproc.pc"
    @ONLY
)

install(
    TARGETS xproc
    EXPORT xprocTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/xproc.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xprocConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xprocConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xproc
)
install(EXPORT xprocTargets
    FILE xprocTargets.cmake
    NAMESPACE xproc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xproc
)
