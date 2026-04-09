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

target_link_libraries(xproc_c PUBLIC xproc::xproc)
target_compile_definitions(xproc_c PRIVATE XPROC_CAPI_PROJECT_VERSION="${PROJECT_VERSION}")

target_include_directories(xproc_c PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

install(
    TARGETS xproc_c
    EXPORT xprocTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${XPROC_CAPI_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
