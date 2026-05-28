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

set(_xproc_python_install_dir_override "")
if(DEFINED CACHE{XPROC_PYTHON_INSTALL_DIR_OVERRIDE})
    get_property(_xproc_python_install_dir_override CACHE XPROC_PYTHON_INSTALL_DIR_OVERRIDE PROPERTY VALUE)
elseif(DEFINED CACHE{XPROC_PYTHON_INSTALL_DIR})
    get_property(_xproc_python_legacy_install_dir CACHE XPROC_PYTHON_INSTALL_DIR PROPERTY VALUE)
    if(NOT _xproc_python_legacy_install_dir STREQUAL "xproc")
        set(_xproc_python_install_dir_override "${_xproc_python_legacy_install_dir}")
    endif()
endif()

set(XPROC_PYTHON_INSTALL_DIR_OVERRIDE "${_xproc_python_install_dir_override}" CACHE STRING
    "Override install destination for the Python xproc package")
mark_as_advanced(XPROC_PYTHON_INSTALL_DIR_OVERRIDE)

set(XPROC_PYTHON_USING_SKBUILD FALSE)
if(XPROC_SKBUILD_WHEEL_INSTALL)
    set(XPROC_PYTHON_USING_SKBUILD TRUE)
endif()

if(XPROC_PYTHON_INSTALL_DIR_OVERRIDE)
    set(XPROC_PYTHON_EFFECTIVE_INSTALL_DIR "${XPROC_PYTHON_INSTALL_DIR_OVERRIDE}")
elseif(XPROC_PYTHON_USING_SKBUILD)
    set(XPROC_PYTHON_EFFECTIVE_INSTALL_DIR "${XPROC_PYTHON_PACKAGE_NAME}")
else()
    set(XPROC_PYTHON_EFFECTIVE_INSTALL_DIR "${XPROC_PYTHON_SITEARCH}/${XPROC_PYTHON_PACKAGE_NAME}")
endif()

set(XPROC_PYTHON_PACKAGE_RELATIVE_FILES
    "__init__.py"
    "__init__.pyi"
    "py.typed"
)
set(XPROC_PYTHON_PACKAGE_FILES "")
foreach(_package_file IN LISTS XPROC_PYTHON_PACKAGE_RELATIVE_FILES)
    list(APPEND XPROC_PYTHON_PACKAGE_FILES
        "${XPROC_PYTHON_SOURCE_DIR}/${_package_file}"
    )
endforeach()
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
    DEPENDS _xproc_pybind ${XPROC_PYTHON_PACKAGE_FILES}
    COMMENT "Staging Python xproc package"
    VERBATIM
)

foreach(_package_file IN LISTS XPROC_PYTHON_PACKAGE_RELATIVE_FILES)
    add_custom_command(TARGET xproc_python_package POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${XPROC_PYTHON_SOURCE_DIR}/${_package_file}"
                "${XPROC_PYTHON_STAGE_DIR}/${_package_file}"
        VERBATIM
    )
endforeach()

function(xproc_python_stage_shared_target target_name)
    get_target_property(_library_type "${target_name}" TYPE)
    if(_library_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET xproc_python_package POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:${target_name}>"
                    "${XPROC_PYTHON_STAGE_DIR}/$<TARGET_FILE_NAME:${target_name}>"
            VERBATIM
        )
        install(TARGETS "${target_name}"
            LIBRARY DESTINATION "${XPROC_PYTHON_EFFECTIVE_INSTALL_DIR}"
            RUNTIME DESTINATION "${XPROC_PYTHON_EFFECTIVE_INSTALL_DIR}"
        )
    endif()
endfunction()

xproc_python_stage_shared_target(xproc)
xproc_python_stage_shared_target(xproc_c)

install(FILES
    ${XPROC_PYTHON_PACKAGE_FILES}
    DESTINATION "${XPROC_PYTHON_EFFECTIVE_INSTALL_DIR}"
)
install(TARGETS _xproc_pybind
    LIBRARY DESTINATION "${XPROC_PYTHON_EFFECTIVE_INSTALL_DIR}"
    RUNTIME DESTINATION "${XPROC_PYTHON_EFFECTIVE_INSTALL_DIR}"
)

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
