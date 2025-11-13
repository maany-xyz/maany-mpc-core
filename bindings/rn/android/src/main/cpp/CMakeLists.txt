cmake_minimum_required(VERSION 3.18)
project(maany_mpc_rn LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# React Native provides imported targets for fbjni and jsi
find_package(fbjni REQUIRED CONFIG)
find_package(ReactAndroid REQUIRED CONFIG)

get_filename_component(MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
get_filename_component(PROJECT_ROOT "${MODULE_DIR}/../../../../../../" ABSOLUTE)

# Build the core MPC library for Android
add_subdirectory(${PROJECT_ROOT}/cpp/third_party/cb-mpc ${CMAKE_CURRENT_BINARY_DIR}/cb-mpc EXCLUDE_FROM_ALL)

add_library(maany_mpc_core STATIC
    ${PROJECT_ROOT}/cpp/src/bridge.cpp
    ${PROJECT_ROOT}/cpp/src/maany_mpc.cc
)

target_include_directories(maany_mpc_core
    PUBLIC ${PROJECT_ROOT}/cpp/include
    PRIVATE ${PROJECT_ROOT}/cpp/third_party/cb-mpc/src
)

target_link_libraries(maany_mpc_core PRIVATE cbmpc)

# JSI / JNI bridge
add_library(maany_mpc_rn SHARED
    ${PROJECT_ROOT}/bindings/rn/ios/cpp/MaanyMpcHostObject.cpp
    ${CMAKE_CURRENT_LIST_DIR}/addon_jni.cpp
)

target_include_directories(maany_mpc_rn PRIVATE
    ${PROJECT_ROOT}/bindings/rn/ios/cpp
    ${PROJECT_ROOT}/cpp/include
)

target_link_libraries(maany_mpc_rn
    PRIVATE
        maany_mpc_core
        fbjni::fbjni
        ReactAndroid::jsi
        log
)
