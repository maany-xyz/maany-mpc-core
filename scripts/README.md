# Generate new Archive Versions

1. iOS
    - Verify `DIST_DIR` inside `scripts/build_ios_core.sh` points at the package you are publishing (`bindings/rn-bare/ios/dist`).
    - From the repo root run `./scripts/build_ios_core.sh`. This builds device + simulator libs, the XCFrameworks, and stages OpenSSL under `ios/dist`.
    - Package the folder for release:
        ```bash
        (cd bindings/rn-bare && tar -czf /tmp/maany-mpc-ios.tar.gz ios/dist)
        ```
2. Android
    - Build OpenSSL for each ABI you ship (default `arm64-v8a`) so `bindings/rn-bare/android/third-party/openssl/<abi>` is refreshed:
        ```bash
        export ANDROID_NDK_ROOT=~/Library/Android/sdk/ndk/<version>
        ABI=arm64-v8a ./scripts/build_android_openssl.sh
        ```
    - Build the native libraries with the same NDK:
        ```bash
        cmake -S . -B build/android-arm64 \
          -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
          -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=24 \
          -DMAANY_BUILD_NODE_ADDON=OFF -DMAANY_BUILD_TESTS=OFF
        cmake --build build/android-arm64 --target maany_mpc_core cbmpc -j
        ```
    - Copy the outputs into `bindings/rn-bare/android/native-libs/<abi>`:
        ```bash
        cp build/android-arm64/libmaany_mpc_core.a bindings/rn-bare/android/native-libs/arm64-v8a/
        cp cpp/third_party/cb-mpc/lib/Release/libcbmpc.a bindings/rn-bare/android/native-libs/arm64-v8a/
        cp bindings/rn-bare/android/third-party/openssl/arm64-v8a/lib/libcrypto.a bindings/rn-bare/android/native-libs/arm64-v8a/
        ```
    - Package the native-libs folder:
        ```bash
        (cd bindings/rn-bare && tar -czf /tmp/maany-mpc-android.tar.gz android/native-libs)
        ```
