cmake_minimum_required(VERSION 3.10)
project(xproc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_library(LIB_RT rt)
    find_package(Threads REQUIRED)
endif()

file(GLOB_RECURSE XPROC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE XPROC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
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

install(TARGETS xproc EXPORT xprocTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT xprocTargets
    FILE xprocConfig.cmake
    NAMESPACE xproc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xproc
)