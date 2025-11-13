#!/usr/bin/env bash
set -euo pipefail

OPENSSL_VERSION="${OPENSSL_VERSION:-3.3.1}"
ANDROID_API="${ANDROID_API:-24}"
ABI="${ABI:-arm64-v8a}"
case "${ABI}" in
  arm64-v8a)
    ANDROID_TARGET="android-arm64"
    ;;
  armeabi-v7a)
    ANDROID_TARGET="android-arm"
    ;;
  x86_64)
    ANDROID_TARGET="android-x86_64"
    ;;
  *)
    echo "Unsupported ABI: ${ABI}" >&2
    exit 1
    ;;
esac
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_ROOT="${OUTPUT_ROOT:-${REPO_ROOT}/bindings/rn/android/third-party/openssl/${ABI}}"

ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}"
if [[ -z "${ANDROID_NDK_ROOT}" ]]; then
  echo "ANDROID_NDK_ROOT (or ANDROID_NDK_HOME) must be set to build OpenSSL for Android." >&2
  exit 1
fi
export ANDROID_NDK_ROOT

UNAME_S="$(uname -s | tr '[:upper:]' '[:lower:]')"
declare -a HOST_CANDIDATES
case "${UNAME_S}" in
  darwin)
    HOST_CANDIDATES=("darwin-arm64" "darwin-x86_64")
    ;;
  linux)
    HOST_CANDIDATES=("linux-x86_64")
    ;;
  *)
    echo "Unsupported host platform: ${UNAME_S}" >&2
    exit 1
    ;;
esac

TOOLCHAIN_DIR=""
for candidate in "${HOST_CANDIDATES[@]}"; do
  if [[ -d "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${candidate}/bin" ]]; then
    TOOLCHAIN_DIR="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${candidate}/bin"
    break
  fi
done

if [[ -z "${TOOLCHAIN_DIR}" ]]; then
  echo "Cannot locate NDK toolchain under ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt (checked ${HOST_CANDIDATES[*]})." >&2
  exit 1
fi
export PATH="${TOOLCHAIN_DIR}:${PATH}"

WORK_DIR="${REPO_ROOT}/.openssl-build"
SRC_DIR="${WORK_DIR}/src"
mkdir -p "${SRC_DIR}"

ARCHIVE="openssl-${OPENSSL_VERSION}.tar.gz"
URL="https://www.openssl.org/source/${ARCHIVE}"

if [[ ! -f "${SRC_DIR}/${ARCHIVE}" ]]; then
  echo "Downloading OpenSSL ${OPENSSL_VERSION}..."
  curl -L "${URL}" -o "${SRC_DIR}/${ARCHIVE}"
fi

if [[ ! -d "${SRC_DIR}/openssl-${OPENSSL_VERSION}" ]]; then
  tar -xf "${SRC_DIR}/${ARCHIVE}" -C "${SRC_DIR}"
fi

pushd "${SRC_DIR}/openssl-${OPENSSL_VERSION}" >/dev/null

./Configure "${ANDROID_TARGET}" no-shared "-D__ANDROID_API__=${ANDROID_API}" "--prefix=${OUTPUT_ROOT}"
make clean
if command -v nproc >/dev/null 2>&1; then
  JOBS="$(nproc)"
else
  JOBS="$(sysctl -n hw.ncpu)"
fi
make -j"${JOBS}"
make install_sw

popd >/dev/null

echo "OpenSSL installed to ${OUTPUT_ROOT}."
