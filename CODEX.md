0) Folder layout (suggested)

Put all iOS-relevant sources under ios/ so CocoaPods can glob them without ../:

@maany/mpc-rn/
├─ package.json
├─ index.ts
└─ ios/
   ├─ MaanyMpc.mm                        # JSI installer (Obj-C++)
   ├─ cpp/                               # your thin hostobject layer
   │  ├─ MaanyMpcHostObject.cpp
   │  └─ MaanyMpcHostObject.h
   └─ native/                            # bring cb-mpc & your core sources here
      ├─ cbmpc/                          # (headers + .c/.cc/.cpp)
      └─ maany_core/                     # (headers + .c/.cc/.cpp)


If cb-mpc is a submodule, you can symlink or copy the specific portable source folders you need into ios/native/…. Avoid platform-specific files that rely on their original CMake; we’ll replicate any required defines in the podspec.

1) Depend on a maintained SSL Pod (no vendoring)

Pick one:

OpenSSL-Universal (widely used, static libs)

BoringSSL-GRPC (if cb-mpc is BoringSSL-compatible)

For OpenSSL, the headers & libs resolve via $(PODS_ROOT)/OpenSSL-Universal/** automatically—no manual search paths required.

2) Podspec: compile all sources directly

Here’s a working template you can drop in as ios/MaanyMpc.podspec (or at repo root; both are fine as long as s.source = { :path => '.' } resolves):

require 'json'
pkg = JSON.parse(File.read(File.join(__dir__, '..', 'package.json'))) rescue JSON.parse(File.read(File.join(__dir__, 'package.json')))

Pod::Spec.new do |s|
  s.name         = 'MaanyMpc'
  s.version      = pkg['version']
  s.summary      = pkg['description']
  s.homepage     = 'https://github.com/maany/maany-mpc-core'
  s.license      = { :type => 'MIT' }
  s.author       = { 'Maany' => 'engineering@maany.xyz' }
  s.source       = { :path => '.' }
  s.platform     = :ios, '13.0'

  # 1) Compile all iOS sources (no ../)
  s.source_files = 'ios/**/*.{mm,m,swift,h,hh,hpp,c,cc,cpp,cxx}'

  # 2) If your headers include relative paths, help the compiler:
  s.header_mappings_dir = 'ios'

  # 3) C++ flags / defines (mirror what CMake was doing)
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY'           => 'libc++',
    # Add any defines you had in CMake, e.g.:
    # 'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) MAANY_USE_OPENSSL=1 SOME_FLAG=1'
  }

  # 4) Link React and SSL; no vendored binaries
  s.dependency 'React-Core'
  s.dependency 'OpenSSL-Universal'

  s.requires_arc = true
end


Notes:

We removed s.vendored_libraries and s.vendored_frameworks. Everything compiles from source.

If cb-mpc requires specific compile DEFINES that used to come from CMake (-DXXXX=1), add them under GCC_PREPROCESSOR_DEFINITIONS.

3) JSI symbol wiring (match signatures)

Your MaanyMpc.mm calls:

maany::rn::installMaanyMpc(*runtime);


So ensure the C++ side matches exactly:

ios/cpp/MaanyMpcHostObject.h

#pragma once
#include <jsi/jsi.h>

namespace maany { namespace rn {
  void installMaanyMpc(facebook::jsi::Runtime& rt);
}}


ios/cpp/MaanyMpcHostObject.cpp

#include "MaanyMpcHostObject.h"

using namespace facebook;

namespace maany { namespace rn {

void installMaanyMpc(jsi::Runtime& rt) {
  // Register HostObjects / globals here
  // e.g., rt.global().setProperty(rt, "MaanyMpc", jsi::Object(rt));
}

}} // namespace maany::rn


The namespace and ref qualifiers must match or Xcode will report “Undefined symbols for architecture arm64”.

4) Consumer app Podfile (static pods + RN defaults)

In your example app / SDK demo:

platform :ios, '13.0'

require_relative '../node_modules/react-native/scripts/react_native_pods'
require_relative '../node_modules/@react-native-community/cli-platform-ios/native_modules'

target 'YourApp' do
  config = use_native_modules!

  # Xcode 16 / iOS 18 friendly; avoids SwiftUICore/CoreAudioTypes noise
  use_frameworks! :linkage => :static

  use_react_native!(
    :path => config[:reactNativePath],
    :hermes_enabled => true
  )

  # If you have any Swift in the app target, ensure at least one .swift exists.

  post_install do |installer|
    react_native_post_install(installer)
  end
end


Then:

cd ios
pod repo update            # optional but helps fetch latest SSL pod
pod install

5) Remove the binary build script from your flow

Delete (or ignore) the build_ios / lipo steps.

Do not ship ios/dist/**.

Your podspec doesn’t reference any vendored artifacts, so there’s nothing to stage.

6) How to migrate CMake options you previously needed

If your CMake used options like:

include dirs (-I/path/to/include)

defines (-DABC=1)

special flags (-fno-exceptions, -fvisibility=hidden)

Translate them into podspec xcconfig:

s.pod_target_xcconfig = {
  'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
  'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) ABC=1 DEF=1',
  'OTHER_CPLUSPLUSFLAGS' => '$(inherited) -fno-exceptions',
  'HEADER_SEARCH_PATHS' => '$(inherited) $(PODS_TARGET_SRCROOT)/ios/native/cbmpc/include $(PODS_TARGET_SRCROOT)/ios/native/maany_core/include'
}


Only add HEADER_SEARCH_PATHS if your headers are not already found relative to the source files. Keep it minimal to avoid conflicts.

7) Common pitfalls & fixes

Undefined JSI symbol → your .cpp didn’t compile (wrong path) or signature mismatch. Keep sources under ios/.

Duplicate -lc++ warning → don’t add -lc++ manually; CocoaPods handles it via CLANG_CXX_LIBRARY=libc++.

SwiftUICore/CoreAudioTypes link warnings → use static pods (use_frameworks! :linkage => :static) in the app’s Podfile.

OpenSSL header not found → CocoaPods should provide include paths. If not, check the exact pod version; add HEADER_SEARCH_PATHS pointing at the pod’s headers (rarely needed).

8) When not to use “build from source”

If:

Your core depends on non-portable build steps (codegen, ASM, custom scripts), or

You want predictable, fast app CI builds and fewer toolchain differences,

then revert to the prebuilt XCFramework approach (previous message), and keep the build script (but output .xcframeworks, not lipo’d .a).