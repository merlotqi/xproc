cmake_minimum_required(VERSION 3.14...3.24)
project(xproc VERSION 0.2.0 LANGUAGES CXX)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
    message(FATAL_ERROR "xproc supports Linux and Windows only (found ${CMAKE_SYSTEM_NAME}); see docs/design.md")
endif()

if(WIN32)
    message(STATUS "xproc: Windows build uses WaitOnAddress / WakeByAddress (requires Windows 8+).")
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

file(GLOB_RECURSE XPROC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM XPROC_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shm/shm_posix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shm/shm_win32.cpp")
if(WIN32)
    list(APPEND XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/shm/shm_win32.cpp")
else()
    list(APPEND XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/shm/shm_posix.cpp")
endif()
add_library(xproc STATIC ${XPROC_HEADERS} ${XPROC_SOURCES})
add_library(xproc::xproc ALIAS xproc)

target_include_directories(xproc PUBLIC 
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(LIB_RT)
        target_link_libraries(xproc PUBLIC ${LIB_RT})
    endif()
    target_link_libraries(xproc PUBLIC Threads::Threads)
endif()

if(MSVC)
    target_compile_definitions(xproc PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

option(XPROC_WITH_NLOHMANN_JSON "Enable nlohmann_json reference codec (find_package nlohmann_json)" OFF)
if(XPROC_WITH_NLOHMANN_JSON)
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(xproc PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(xproc PUBLIC XPROC_WITH_NLOHMANN_JSON)
endif()

option(XPROC_WITH_PROTOBUF "Enable Protobuf reference codec (find_package Protobuf)" OFF)
if(XPROC_WITH_PROTOBUF)
    find_package(Protobuf REQUIRED)
    if(TARGET protobuf::libprotobuf)
        target_link_libraries(xproc PUBLIC protobuf::libprotobuf)
    else()
        target_link_libraries(xproc PUBLIC ${Protobuf_LIBRARIES})
        target_include_directories(xproc PUBLIC ${Protobuf_INCLUDE_DIRS})
    endif()
    target_compile_definitions(xproc PUBLIC XPROC_WITH_PROTOBUF)
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

install(TARGETS xproc EXPORT xprocTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
