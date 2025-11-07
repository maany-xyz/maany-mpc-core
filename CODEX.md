Why you’re hitting linker issues

“Universal” .a via lipo is outdated for iOS 17/18 & Xcode 15/16—arm64-sim vs arm64 (device) needs an XCFramework, not a lipo’d .a. That’s exactly why the linker can’t resolve slices and why you see weird auto-link warnings (SwiftUICore/CoreAudioTypes).

What to do instead (keep the script, but change output)

Keep the idea, but output XCFrameworks and reference them in your podspec:

Build device + sim archives (your CMake step is fine).

Replace the lipo -create … -output lib*.a part with:

xcodebuild -create-xcframework \
  -library "$device_core" -headers "$ROOT_DIR/include" \
  -library "$sim_core"    -headers "$ROOT_DIR/include" \
  -output "$DIST_DIR/libmaany_mpc_core.xcframework"

xcodebuild -create-xcframework \
  -library "$device_cbmpc" -headers "$ROOT_DIR/include_cbmpc" \
  -library "$sim_cbmpc"    -headers "$ROOT_DIR/include_cbmpc" \
  -output "$DIST_DIR/libcbmpc.xcframework"


(Adjust header paths to wherever your public headers live.)

Keep staging openssl.xcframework (what your script already does).

Update your podspec (in @maany/mpc-rn) to use only vendored_frameworks:

s.vendored_frameworks = [
  'dist/libmaany_mpc_core.xcframework',
  'dist/libcbmpc.xcframework',
  'dist/openssl.xcframework'
]


…and remove vendored_libraries and custom LIBRARY_SEARCH_PATHS.

Make sure your C++ JSI files are inside ios/ and included via:

s.source_files = 'ios/**/*.{mm,m,swift,h,hh,hpp,cpp,cxx}'

Where each piece lives

@maany/mpc-rn (RN bridge): owns this build script + emits XCFrameworks under ios/dist + exposes the JSI glue (MaanyMpc.mm + cpp).

@maany/coordinator-rn: depends on @maany/mpc-rn, no native build steps.

If you don’t want prebuilt binaries

You can drop the script and:

Add all native sources (including OpenSSL) as CocoaPods deps / sources and build from source in the consumer app. This avoids shipping binaries but complicates Pod setup and build times.