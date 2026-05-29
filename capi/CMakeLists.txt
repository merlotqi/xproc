set(XPROC_CAPI_HEADERS
    xproc_c.h
)

set(XPROC_CAPI_SOURCES
    xproc_c.cpp
)

if(XPROC_BUILD_SHARED)
    add_library(xproc_c SHARED ${XPROC_CAPI_HEADERS} ${XPROC_CAPI_SOURCES})
    target_compile_definitions(xproc_c PRIVATE XPROC_CAPI_BUILDING XPROC_CAPI_SHARED)
    target_compile_definitions(xproc_c PUBLIC XPROC_CAPI_SHARED)
else()
    add_library(xproc_c STATIC ${XPROC_CAPI_HEADERS} ${XPROC_CAPI_SOURCES})
endif()

add_library(xproc::xproc_c ALIAS xproc_c)

if(APPLE AND XPROC_BUILD_SHARED)
    set_target_properties(xproc_c PROPERTIES
        INSTALL_RPATH "@loader_path"
    )
endif()

target_link_libraries(xproc_c PUBLIC xproc::xproc)
target_compile_definitions(xproc_c PRIVATE XPROC_CAPI_PROJECT_VERSION="${PROJECT_VERSION}")

target_include_directories(xproc_c PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# Windows + DLL: align xproc_c.dll output directory with xproc.dll so both are
# discoverable by test/example executables during gtest discovery and direct runs.
if(WIN32 AND XPROC_BUILD_SHARED)
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(TOUPPER "${_cfg}" _CFG_UPPER)
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG_UPPER} "${CMAKE_BINARY_DIR}/${_cfg}")
        endforeach()
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    endif()
endif()

if(NOT XPROC_SKBUILD_WHEEL_INSTALL)
install(
    TARGETS xproc_c
    EXPORT xprocTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${XPROC_CAPI_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
