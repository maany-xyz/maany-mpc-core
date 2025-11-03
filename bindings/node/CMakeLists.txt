cmake_minimum_required(VERSION 3.20)

project(maany_mpc_node LANGUAGES CXX)

if(NOT TARGET maany_mpc_core)
  message(FATAL_ERROR "maany_mpc_node requires the maany_mpc_core target to be defined")
endif()

find_program(NODE_EXECUTABLE node)
if(NOT NODE_EXECUTABLE)
  message(FATAL_ERROR "Failed to locate the 'node' executable on PATH")
endif()

execute_process(
  COMMAND ${NODE_EXECUTABLE} -p "require('node:path').join(process.execPath, '..', '..', 'include', 'node')"
  OUTPUT_VARIABLE NODE_INCLUDE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT EXISTS ${NODE_INCLUDE_DIR})
  message(FATAL_ERROR "Node include directory not found: ${NODE_INCLUDE_DIR}")
endif()

add_library(maany_mpc_node SHARED addon.cc)

target_compile_features(maany_mpc_node PRIVATE cxx_std_17)
target_compile_definitions(maany_mpc_node PRIVATE NAPI_VERSION=8)
target_include_directories(maany_mpc_node PRIVATE ${NODE_INCLUDE_DIR})

target_link_libraries(maany_mpc_node PRIVATE maany_mpc_core)

set_target_properties(maany_mpc_node PROPERTIES
  PREFIX ""
  SUFFIX ".node"
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bindings/node
)

if(APPLE)
  target_link_options(maany_mpc_node PRIVATE "-undefined" "dynamic_lookup")
endif()

install(TARGETS maany_mpc_node DESTINATION bindings/node)
