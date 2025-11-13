Prebuilt OpenSSL slices live here: one folder per ABI (e.g., arm64-v8a/include, arm64-v8a/libcrypto.a).
These files are bundled with the npm package so consuming apps do not need to compile OpenSSL.
Use scripts/build_android_openssl.sh to refresh the artifacts before publishing.
