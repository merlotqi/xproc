execute_process(
  COMMAND node -p "require('node-addon-api').include_dir"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE NODE_ADDON_API_RESULT
  OUTPUT_VARIABLE NODE_ADDON_API_INCLUDE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT NODE_ADDON_API_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Failed to resolve node-addon-api. Run `npm ci` in ${CMAKE_CURRENT_SOURCE_DIR} "
    "or configure with -DXPROC_BUILD_NODE=OFF."
  )
endif()

set(NODE_ADDON_API_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${NODE_ADDON_API_INCLUDE_DIR}")

if(NODE_INCLUDE_DIR AND EXISTS "${NODE_INCLUDE_DIR}/node_api.h")
  set(_node_api_include "${NODE_INCLUDE_DIR}")
else()
  execute_process(
    COMMAND node -p "require('node-api-headers').include_dir"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE NODE_HDR_RESULT
    OUTPUT_VARIABLE _node_api_include
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT NODE_HDR_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Failed to resolve node-api-headers. Run `npm ci` in ${CMAKE_CURRENT_SOURCE_DIR} "
      "(adds node_api.h via the node-api-headers package) or set NODE_INCLUDE_DIR to that directory."
    )
  endif()
endif()

if(NOT EXISTS "${_node_api_include}/node_api.h")
  message(FATAL_ERROR
    "node_api.h not found under ${_node_api_include}. "
    "Check node-api-headers is installed, or set -DNODE_INCLUDE_DIR to the folder containing node_api.h."
  )
endif()

message(STATUS "Node-API headers: ${_node_api_include}")

execute_process(
  COMMAND node -p "require('path').dirname(require.resolve('node-api-headers/package.json'))"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE _nah_root_result
  OUTPUT_VARIABLE _node_api_headers_root
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _nah_root_result EQUAL 0)
  message(FATAL_ERROR "Could not resolve node-api-headers package root.")
endif()
file(TO_CMAKE_PATH "${_node_api_headers_root}" _node_api_headers_root)

add_library(xproc_node MODULE
  src/node_binding.cpp
)

target_include_directories(xproc_node PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${_node_api_include}
  ${NODE_ADDON_API_INCLUDE_DIR}
)

target_compile_definitions(xproc_node PRIVATE
  NAPI_DISABLE_CPP_EXCEPTIONS
)

target_compile_features(xproc_node PRIVATE cxx_std_17)

target_link_libraries(xproc_node PRIVATE
  xproc::xproc_c
)

set_target_properties(xproc_node PROPERTIES
  PREFIX ""
  SUFFIX ".node"
  OUTPUT_NAME "xproc"
)

if(APPLE)
  target_link_options(xproc_node PRIVATE "-undefined" "dynamic_lookup")
endif()

# MSVC: N-API entry points live in node.exe; link an import library built from official .def files.
if(WIN32 AND MSVC)
  # node_api.def lists napi_* exports from NODE.EXE; enough for node-addon-api C++ wrapper.
  set(_napi_def_node "${_node_api_headers_root}/def/node_api.def")
  if(NOT EXISTS "${_napi_def_node}")
    message(FATAL_ERROR
      "Missing ${_napi_def_node}. Run npm ci in ${CMAKE_CURRENT_SOURCE_DIR}."
    )
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
      set(_napi_machine arm64)
    else()
      set(_napi_machine x64)
    endif()
  else()
    set(_napi_machine x86)
  endif()
  get_filename_component(_link_dir "${CMAKE_LINKER}" DIRECTORY)
  set(_lib_exe "${_link_dir}/lib.exe")
  if(NOT EXISTS "${_lib_exe}")
    get_filename_component(_link_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_lib_exe "${_link_dir}/lib.exe")
  endif()
  if(NOT EXISTS "${_lib_exe}")
    message(FATAL_ERROR "Could not find lib.exe next to CMAKE_LINKER or CMAKE_CXX_COMPILER.")
  endif()
  set(_napi_import_lib "${CMAKE_CURRENT_BINARY_DIR}/node_napi_import.lib")
  execute_process(
    COMMAND "${_lib_exe}" /nologo
      "/def:${_napi_def_node}"
      "/out:${_napi_import_lib}"
      "/machine:${_napi_machine}"
    RESULT_VARIABLE _napi_lib_rc
    OUTPUT_VARIABLE _napi_lib_out
    ERROR_VARIABLE _napi_lib_err
  )
  if(NOT _napi_lib_rc EQUAL 0)
    message(FATAL_ERROR
      "lib.exe failed to build N-API import library (rc=${_napi_lib_rc}):\n${_napi_lib_err}\n${_napi_lib_out}"
    )
  endif()
  target_link_libraries(xproc_node PRIVATE "${_napi_import_lib}")
endif()
