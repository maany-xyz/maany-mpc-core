require 'json'

package = JSON.parse(File.read(File.join(__dir__, '..', 'package.json')))

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
    'MaanyMpc.mm',
    '../cpp/MaanyMpcHostObject.cpp',
    '../cpp/MaanyMpcHostObject.h'
  ]

  s.vendored_libraries = [
    'dist/universal/libmaany_mpc_core.a',
    'dist/universal/libcbmpc.a'
  ]

  s.vendored_frameworks = ['dist/openssl.xcframework']
  s.requires_arc = true

  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'HEADER_SEARCH_PATHS' => '$(inherited) $(PODS_TARGET_SRCROOT)/../cpp',
    'LIBRARY_SEARCH_PATHS' => '$(inherited) $(PODS_TARGET_SRCROOT)/dist/universal'
  }

  s.dependency 'React-Core'
end
