cmake_minimum_required(VERSION 3.14...3.24)
project(xproc VERSION 1.0.0 LANGUAGES CXX)

# Visual Studio generator: default to x64 when -A is omitted, avoiding winnt.h C1189 "No Target Architecture".
if(WIN32 AND MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    if(NOT CMAKE_GENERATOR_PLATFORM)
        set(CMAKE_GENERATOR_PLATFORM "x64" CACHE STRING "Visual Studio platform selection")
    endif()
endif()

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
    message(FATAL_ERROR "xproc supports Linux, Windows, MacOS only (found ${CMAKE_SYSTEM_NAME}); see docs/design.md")
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

file(GLOB_RECURSE XPROC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM XPROC_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/shm_posix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/shm_win32.cpp")
if(WIN32)
    list(APPEND XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/shm_win32.cpp")
else()
    list(APPEND XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/shm_posix.cpp")
endif()

option(XPROC_BUILD_SHARED "Build libxproc as a shared library" OFF)

# Windows + DLL: place xproc.dll next to test/example/benchmark executables so GoogleTest
# discovery does not fail with 0xc0000135 (STATUS_DLL_NOT_FOUND).
if(WIN32 AND XPROC_BUILD_SHARED)
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(TOUPPER "${_cfg}" _CFG_UPPER)
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG_UPPER} "${CMAKE_BINARY_DIR}/${_cfg}")
            set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_CFG_UPPER} "${CMAKE_BINARY_DIR}/${_cfg}")
            set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_CFG_UPPER} "${CMAKE_BINARY_DIR}/${_cfg}")
        endforeach()
    else()
        if(NOT CMAKE_BUILD_TYPE)
            set(CMAKE_BUILD_TYPE Release)
        endif()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    endif()
endif()

if(XPROC_BUILD_SHARED)
    add_library(xproc SHARED ${XPROC_HEADERS} ${XPROC_SOURCES})
else()
    add_library(xproc STATIC ${XPROC_HEADERS} ${XPROC_SOURCES})
endif()
add_library(xproc::xproc ALIAS xproc)

if(XPROC_BUILD_SHARED AND WIN32)
    set_target_properties(xproc PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_include_directories(xproc PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

set(XPROC_PKGCONFIG_LIBS_PRIVATE "")
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(LIB_RT)
        target_link_libraries(xproc PUBLIC ${LIB_RT})
        set(XPROC_PKGCONFIG_LIBS_PRIVATE "-pthread -lrt")
    else()
        set(XPROC_PKGCONFIG_LIBS_PRIVATE "-pthread")
    endif()
    target_link_libraries(xproc PUBLIC Threads::Threads)
endif()

if(WIN32)
    target_link_libraries(xproc PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_definitions(xproc PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xproc PRIVATE /utf-8)
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

option(XPROC_BUILD_CAPI "Build xproc_c C ABI wrapper" ON)
option(XPROC_BUILD_CAPI_SHARED "Build xproc_c as a shared library (needed for P/Invoke-style bindings)" ${XPROC_BUILD_SHARED})
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

option(XPROC_BUILD_PYTHON "Stage Python binding package" ON)
if(XPROC_BUILD_PYTHON)
    add_subdirectory(Python)
endif()

if(TARGET xproc_run_tests)
    if(TARGET xproc_node)
        add_dependencies(xproc_run_tests xproc_node)
    endif()
    if(TARGET xproc_python_package)
        add_dependencies(xproc_run_tests xproc_python_package)
    endif()
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
