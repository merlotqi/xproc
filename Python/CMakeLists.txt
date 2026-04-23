if(NOT XPROC_BUILD_CAPI)
    message(FATAL_ERROR "XPROC_BUILD_PYTHON requires XPROC_BUILD_CAPI=ON")
endif()

include(FetchContent)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
set(PYBIND11_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pybind11)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c
            "import pathlib, sys, sysconfig; purelib = pathlib.Path(sysconfig.get_path('purelib')).resolve(); prefix = pathlib.Path(sys.prefix).resolve(); print(purelib.relative_to(prefix).as_posix() if purelib.is_relative_to(prefix) else purelib.as_posix())"
    OUTPUT_VARIABLE XPROC_PYTHON_SITEARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT XPROC_PYTHON_SITEARCH)
    set(XPROC_PYTHON_SITEARCH "${CMAKE_INSTALL_LIBDIR}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages")
endif()

set(XPROC_PYTHON_PACKAGE_NAME xproc)
set(XPROC_PYTHON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${XPROC_PYTHON_PACKAGE_NAME}")
set(XPROC_PYTHON_STAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/stage")
set(XPROC_PYTHON_STAGE_DIR "${XPROC_PYTHON_STAGE_ROOT}/${XPROC_PYTHON_PACKAGE_NAME}")
set(XPROC_PYTHON_INSTALL_DIR "${XPROC_PYTHON_SITEARCH}/${XPROC_PYTHON_PACKAGE_NAME}")
set(XPROC_PYTHON_PACKAGE_FILES
    "${XPROC_PYTHON_SOURCE_DIR}/__init__.py"
    "${XPROC_PYTHON_SOURCE_DIR}/__init__.pyi"
    "${XPROC_PYTHON_SOURCE_DIR}/py.typed"
)
file(MAKE_DIRECTORY "${XPROC_PYTHON_STAGE_DIR}")

get_target_property(XPROC_LIBRARY_TYPE xproc TYPE)
get_target_property(XPROC_CAPI_LIBRARY_TYPE xproc_c TYPE)

if(XPROC_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    set_target_properties(xproc PROPERTIES POSITION_INDEPENDENT_CODE ON)
elseif(XPROC_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    set_target_properties(xproc PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
if(XPROC_CAPI_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    set_target_properties(xproc_c PROPERTIES POSITION_INDEPENDENT_CODE ON)
elseif(XPROC_CAPI_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    set_target_properties(xproc_c PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

pybind11_add_module(_xproc_pybind MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/python_binding.cpp"
)
target_link_libraries(_xproc_pybind PRIVATE xproc::xproc_c)
target_compile_features(_xproc_pybind PRIVATE cxx_std_17)
set_target_properties(_xproc_pybind PROPERTIES
    OUTPUT_NAME "_xproc_pybind"
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

if(CMAKE_CONFIGURATION_TYPES)
    foreach(_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${_cfg}" _CFG_UPPER)
        set_target_properties(_xproc_pybind PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${_CFG_UPPER} "${XPROC_PYTHON_STAGE_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_${_CFG_UPPER} "${XPROC_PYTHON_STAGE_DIR}"
        )
    endforeach()
else()
    set_target_properties(_xproc_pybind PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${XPROC_PYTHON_STAGE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${XPROC_PYTHON_STAGE_DIR}"
    )
endif()

add_custom_target(xproc_python_package ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${XPROC_PYTHON_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${XPROC_PYTHON_PACKAGE_FILES} "${XPROC_PYTHON_STAGE_DIR}"
    DEPENDS _xproc_pybind ${XPROC_PYTHON_PACKAGE_FILES}
    COMMENT "Staging Python xproc package"
    VERBATIM
)

if(XPROC_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET xproc_python_package POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:xproc>"
                "${XPROC_PYTHON_STAGE_DIR}/$<TARGET_FILE_NAME:xproc>"
        VERBATIM
    )
endif()

if(XPROC_CAPI_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET xproc_python_package POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:xproc_c>"
                "${XPROC_PYTHON_STAGE_DIR}/$<TARGET_FILE_NAME:xproc_c>"
        VERBATIM
    )
endif()

install(FILES
    ${XPROC_PYTHON_PACKAGE_FILES}
    DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
)
install(TARGETS _xproc_pybind
    LIBRARY DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
    RUNTIME DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
)

if(XPROC_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    install(TARGETS xproc
        LIBRARY DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
        RUNTIME DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
    )
endif()

if(XPROC_CAPI_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    install(TARGETS xproc_c
        LIBRARY DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
        RUNTIME DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
    )
endif()

if(BUILD_TESTING)
    add_test(
        NAME xproc_python_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_test.py"
                --module-dir "${XPROC_PYTHON_STAGE_ROOT}"
    )
    set_tests_properties(xproc_python_smoke PROPERTIES
        LABELS "python;smoke"
    )
endif()
