require 'json'

package = JSON.parse(File.read(File.join(__dir__, '..', 'package.json')))

Pod::Spec.new do |s|
  s.name         = 'MaanyMpcCoordinatorRn'
  s.version      = package['version']
  s.summary      = 'Secure storage utilities for Maany MPC coordinator (React Native)'
  s.homepage     = 'https://github.com/maany/maany-mpc-core'
  s.license      = { :type => 'MIT' }
  s.author       = { 'Maany' => 'engineering@maany.xyz' }
  s.source       = { :path => '.' }
  s.platform     = :ios, '13.0'

  s.source_files = [
    'MaanyMpcSecureStorage.m',
    'MaanyMpcSecureStorage.h'
  ]

  s.requires_arc = true

  s.dependency 'React-Core'
end
