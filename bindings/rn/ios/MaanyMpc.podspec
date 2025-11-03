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
    '../cpp/*.cpp',
    '../../cpp/include/**/*.h',
    '../../cpp/src/**/*.{cc,cpp}',
    '../../cpp/third_party/cb-mpc/src/**/*.{cc,cpp,c}',
    '../../cpp/third_party/cb-mpc/src/**/*.h'
  ]

  s.exclude_files = [
    '../../cpp/third_party/cb-mpc/src/tests/**'
  ]

  s.public_header_files = ['../cpp/MaanyMpcHostObject.h', '../../cpp/include/**/*.h']
  s.header_mappings_dir = '../../cpp/include'

  s.requires_arc = false
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'HEADER_SEARCH_PATHS' => '$(PODS_TARGET_SRCROOT)/../cpp $(PODS_TARGET_SRCROOT)/../../cpp/include $(PODS_TARGET_SRCROOT)/../../cpp/third_party/cb-mpc/src'
  }

  s.dependency 'React-Core'
end
