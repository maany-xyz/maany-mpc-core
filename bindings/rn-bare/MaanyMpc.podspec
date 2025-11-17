require 'json'

package = JSON.parse(File.read(File.join(__dir__, 'package.json')))

Pod::Spec.new do |s|
  s.name         = 'MaanyMpc'
  s.version      = package['version']
  s.summary      = package['description']
  s.homepage     = 'https://github.com/maany/maany-mpc-core'
  s.license      = { :type => 'MIT' }
  s.author       = { 'Maany' => 'engineering@maany.xyz' }
  s.source       = { :path => '.' }
  s.platform     = :ios, '13.0'

  s.source_files = [
    'ios/MaanyMpc.mm',
    'ios/cpp/**/*.{h,hh,hpp,c,cc,cpp,cxx}'
  ]
  s.exclude_files = 'ios/dist/**/*'

  s.vendored_libraries = [
    'ios/dist/device/libmaany_mpc_core.a',
    'ios/dist/simulator/libmaany_mpc_core.a',
    'ios/dist/device/libcbmpc.a',
    'ios/dist/simulator/libcbmpc.a'
  ]
  s.vendored_frameworks = ['ios/dist/openssl.xcframework']
  s.requires_arc = true

  maany_root = "$(PODS_ROOT)/../../node_modules/#{package['name']}/ios"
  xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'HEADER_SEARCH_PATHS' => '$(inherited) $(PODS_TARGET_SRCROOT)/ios/cpp $(PODS_TARGET_SRCROOT)/ios/cpp/include',
    'LIBRARY_SEARCH_PATHS[sdk=iphoneos*]' => "$(inherited) #{maany_root}/dist/device",
    'LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*]' => "$(inherited) #{maany_root}/dist/simulator",
    'FRAMEWORK_SEARCH_PATHS[sdk=iphoneos*]' => "$(inherited) #{maany_root}/dist/openssl.xcframework/ios-arm64_arm64e",
    'FRAMEWORK_SEARCH_PATHS[sdk=iphonesimulator*]' => "$(inherited) #{maany_root}/dist/openssl.xcframework/ios-arm64_x86_64-simulator",
    'OTHER_LDFLAGS[sdk=iphoneos*]' => "$(inherited) -force_load #{maany_root}/dist/device/libmaany_mpc_core.a -force_load #{maany_root}/dist/device/libcbmpc.a -framework openssl",
    'OTHER_LDFLAGS[sdk=iphonesimulator*]' => "$(inherited) -force_load #{maany_root}/dist/simulator/libmaany_mpc_core.a -force_load #{maany_root}/dist/simulator/libcbmpc.a -framework openssl"
  }

  s.pod_target_xcconfig = xcconfig
  s.user_target_xcconfig = xcconfig

  s.dependency 'React-Core'
end
