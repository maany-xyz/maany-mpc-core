cmake_minimum_required(VERSION 3.18)
project(maany_mpc_rn LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# React Native provides imported targets for fbjni and jsi
find_package(fbjni REQUIRED CONFIG)
find_package(ReactAndroid REQUIRED CONFIG)

get_filename_component(MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
get_filename_component(PACKAGE_ROOT "${MODULE_DIR}/../../.." REALPATH)

set(MAANY_CPP_DIR "${PACKAGE_ROOT}/ios/cpp")
set(MAANY_INCLUDE_DIR "${MAANY_CPP_DIR}/include")
set(MAANY_NATIVE_LIBS_DIR "${PACKAGE_ROOT}/android/native-libs")

if(NOT DEFINED ANDROID_ABI)
  message(FATAL_ERROR "ANDROID_ABI is not defined; ensure the build is through Gradle/NDK")
endif()

set(MAANY_SLICE_DIR "${MAANY_NATIVE_LIBS_DIR}/${ANDROID_ABI}")
set(MAANY_CORE_LIB "${MAANY_SLICE_DIR}/libmaany_mpc_core.a")
set(MAANY_CBMPC_LIB "${MAANY_SLICE_DIR}/libcbmpc.a")
set(MAANY_OPENSSL_LIB "${MAANY_SLICE_DIR}/libcrypto.a")

foreach(required_file IN LISTS MAANY_CORE_LIB MAANY_CBMPC_LIB MAANY_OPENSSL_LIB)
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR
        "Missing native dependency ${required_file}. "
        "Run the fetch-native-artifacts script before building.")
  endif()
endforeach()

add_library(maany_mpc_core STATIC IMPORTED)
set_target_properties(maany_mpc_core PROPERTIES IMPORTED_LOCATION "${MAANY_CORE_LIB}")

add_library(maany_cbmpc STATIC IMPORTED)
set_target_properties(maany_cbmpc PROPERTIES IMPORTED_LOCATION "${MAANY_CBMPC_LIB}")

add_library(maany_openssl STATIC IMPORTED)
set_target_properties(maany_openssl PROPERTIES IMPORTED_LOCATION "${MAANY_OPENSSL_LIB}")

# JSI / JNI bridge
add_library(maany_mpc_rn SHARED
    "${MAANY_CPP_DIR}/MaanyMpcHostObject.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/addon_jni.cpp"
)

target_include_directories(maany_mpc_rn PRIVATE
    "${MAANY_CPP_DIR}"
    "${MAANY_INCLUDE_DIR}"
)

target_link_libraries(maany_mpc_rn
    PRIVATE
        maany_mpc_core
        maany_cbmpc
        maany_openssl
        fbjni::fbjni
        ReactAndroid::jsi
        log
)
