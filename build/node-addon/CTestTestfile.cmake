# CMake generated Testfile for 
# Source directory: /Users/markokuncic/Desktop/Tools/maany-mpc-core
# Build directory: /Users/markokuncic/Desktop/Tools/maany-mpc-core/build/node-addon
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[dkg_roundtrip]=] "/Users/markokuncic/Desktop/Tools/maany-mpc-core/build/node-addon/dkg_roundtrip")
set_tests_properties([=[dkg_roundtrip]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/markokuncic/Desktop/Tools/maany-mpc-core/CMakeLists.txt;26;add_test;/Users/markokuncic/Desktop/Tools/maany-mpc-core/CMakeLists.txt;0;")
subdirs("cpp/third_party/cb-mpc")
subdirs("bindings/node")
