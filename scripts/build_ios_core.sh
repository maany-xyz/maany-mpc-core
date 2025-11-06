#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="$ROOT_DIR/build/ios"
DIST_DIR="$ROOT_DIR/bindings/rn/ios/dist"
IOS_DEPLOYMENT_TARGET="13.0"
OPENSSL_VERSION="3.5.501"
OPENSSL_ARCHIVE="openssl.xcframework.zip"
OPENSSL_URL="https://github.com/partout-io/openssl-apple/releases/download/${OPENSSL_VERSION}/${OPENSSL_ARCHIVE}"
OPENSSL_PREFIX="$BUILD_ROOT/openssl.xcframework"

function ensure_openssl() {
  if [[ -d "$OPENSSL_PREFIX" ]]; then
    return
  fi
  mkdir -p "$BUILD_ROOT"
  local archive_path="$BUILD_ROOT/${OPENSSL_ARCHIVE}"
  if [[ ! -f "$archive_path" ]]; then
    echo "Downloading OpenSSL ${OPENSSL_VERSION}...
    URL: ${OPENSSL_URL}"
    curl -L "$OPENSSL_URL" -o "$archive_path"
  fi
  rm -rf "$OPENSSL_PREFIX"
  unzip -q "$archive_path" -d "$BUILD_ROOT"
}

function cmake_configure() {
  local kind="$1"
  local build_dir="$2"
  local sysroot arch
  local openssl_dir

  if [[ "$kind" == "device" ]]; then
    sysroot="iphoneos"
    arch="arm64"
    openssl_dir="$OPENSSL_PREFIX/ios-arm64"
  else
    sysroot="iphonesimulator"
    arch="arm64;x86_64"
    openssl_dir="$OPENSSL_PREFIX/ios-arm64_x86_64-simulator"
  fi

  rm -rf "$build_dir"

  local clang clangxx
  clang="$(xcrun --sdk "${sysroot}" -f clang)"
  clangxx="$(xcrun --sdk "${sysroot}" -f clang++)"

  ensure_openssl

  cmake -S "$ROOT_DIR" \
        -B "$build_dir" \
        -G Xcode \
        -DMAANY_BUILD_NODE_ADDON=OFF \
        -DMAANY_BUILD_TESTS=OFF \
        -DBUILD_TESTING=OFF \
        -DOPENSSL_ROOT_DIR="$openssl_dir" \
        -DOPENSSL_INCLUDE_DIR="$OPENSSL_PREFIX/Headers" \
        -DOPENSSL_CRYPTO_LIBRARY="$openssl_dir/libcrypto.a" \
        -DOPENSSL_SSL_LIBRARY="$openssl_dir/libssl.a" \
        -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="${sysroot}" \
        -DCMAKE_OSX_ARCHITECTURES="${arch}" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="${clang}" \
        -DCMAKE_CXX_COMPILER="${clangxx}" \
        -DCMAKE_XCODE_ATTRIBUTE_DERIVED_DATA_PATH="${build_dir}/DerivedData"
}

function cmake_build() {
  local build_dir="$1"
  cmake --build "$build_dir" --config Release --parallel --target maany_mpc_core cbmpc
}

function find_lib() {
  local build_dir="$1"
  local name="$2"
  local candidate

  candidate="$build_dir/Release-iphoneos/lib${name}.a"
  if [[ -f "$candidate" ]]; then
    echo "$candidate"
    return
  fi

  candidate="$build_dir/Release-iphonesimulator/lib${name}.a"
  if [[ -f "$candidate" ]]; then
    echo "$candidate"
    return
  fi

  candidate="$build_dir/Release/lib${name}.a"
  if [[ -f "$candidate" ]]; then
    echo "$candidate"
    return
  fi

  echo ""  # not found
}

function ensure_lib() {
  local build_dir="$1"
  local name="$2"
  local path

  path="$(find_lib "$build_dir" "$name")"
  if [[ -z "$path" ]]; then
    echo "error: failed to locate lib${name}.a in $build_dir" >&2
    exit 1
  fi
  echo "$path"
}

function stage_openssl_artifacts() {
  ensure_openssl
  rm -rf "$DIST_DIR/openssl.xcframework"
  cp -R "$OPENSSL_PREFIX" "$DIST_DIR/openssl.xcframework"
}

function prepare_dist() {
  rm -rf "$DIST_DIR"
  mkdir -p "$DIST_DIR/device" "$DIST_DIR/simulator" "$DIST_DIR/universal"
}

function build_ios() {
  ensure_openssl

  local device_build="$BUILD_ROOT/device"
  local sim_build="$BUILD_ROOT/simulator"

  cmake_configure device "$device_build"
  cmake_configure simulator "$sim_build"

  cmake_build "$device_build"
  cmake_build "$sim_build"

  local device_core sim_core device_cbmpc sim_cbmpc
  device_core="$(ensure_lib "$device_build" "maany_mpc_core")"
  sim_core="$(ensure_lib "$sim_build" "maany_mpc_core")"
  device_cbmpc="$(ensure_lib "$device_build" "cbmpc")"
  sim_cbmpc="$(ensure_lib "$sim_build" "cbmpc")"

  prepare_dist

  cp "$device_core" "$DIST_DIR/device/"
  cp "$sim_core" "$DIST_DIR/simulator/"
  cp "$device_cbmpc" "$DIST_DIR/device/"
  cp "$sim_cbmpc" "$DIST_DIR/simulator/"

  local lipo
  lipo="$(xcrun --sdk iphoneos -f lipo)"

  "$lipo" -create "$device_core" "$sim_core" -output "$DIST_DIR/universal/libmaany_mpc_core.a"
  "$lipo" -create "$device_cbmpc" "$sim_cbmpc" -output "$DIST_DIR/universal/libcbmpc.a"

  stage_openssl_artifacts

  echo "iOS static libraries written to $DIST_DIR"
}

build_ios
