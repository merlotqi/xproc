file(GLOB_RECURSE XPROC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/../include/*.hpp")
file(GLOB_RECURSE XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM XPROC_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/core/shm_posix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/shm_win32.cpp")
if(WIN32)
    list(APPEND XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/core/shm_win32.cpp")
else()
    list(APPEND XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/core/shm_posix.cpp")
endif()

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

if(APPLE AND XPROC_BUILD_SHARED)
    set_target_properties(xproc PROPERTIES
        INSTALL_RPATH "@loader_path"
    )
endif()

target_include_directories(xproc PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>"
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
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    # macOS: pthread is part of libSystem — no extra link flags or pkg-config deps needed.
elseif(WIN32)
    # Windows: ws2_32 is handled as a CMake PUBLIC link dependency; pkg-config is uncommon here.
endif()

if(WIN32)
    target_link_libraries(xproc PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_definitions(xproc PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xproc PRIVATE /utf-8)
endif()

set(XPROC_PKGCONFIG_LIBS_PRIVATE ${XPROC_PKGCONFIG_LIBS_PRIVATE} PARENT_SCOPE)
