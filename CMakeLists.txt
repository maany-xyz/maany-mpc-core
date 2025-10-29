cmake_minimum_required(VERSION 3.22)
project(maany_mpc_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(BUILD_TESTS OFF CACHE BOOL "Disable cb-mpc tests" FORCE)
enable_testing()

# Add cb-mpc (vendored)
add_subdirectory(cpp/third_party/cb-mpc EXCLUDE_FROM_ALL)

# Our library
add_library(maany_mpc_core
    cpp/src/bridge.cpp
    cpp/src/maany_mpc.cc
)

target_include_directories(maany_mpc_core PUBLIC cpp/include)
target_include_directories(maany_mpc_core PRIVATE cpp/third_party/cb-mpc/src)
target_link_libraries(maany_mpc_core PRIVATE cbmpc)  # actual target name may differ

add_executable(dkg_roundtrip tests/cpp/dkg_roundtrip.cpp)
target_include_directories(dkg_roundtrip PRIVATE cpp/third_party/cb-mpc/src ${OPENSSL_INCLUDE_DIR})
target_link_libraries(dkg_roundtrip PRIVATE maany_mpc_core)
add_test(NAME dkg_roundtrip COMMAND dkg_roundtrip)
