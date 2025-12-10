# CMake generated Testfile for 
# Source directory: /home/runner/work/DBPFKit/DBPFKit
# Build directory: /home/runner/work/DBPFKit/DBPFKit/_codeql_build_dir
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[AllTests]=] "/home/runner/work/DBPFKit/DBPFKit/_codeql_build_dir/SC4RULParserTests")
set_tests_properties([=[AllTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/DBPFKit/DBPFKit/CMakeLists.txt;115;add_test;/home/runner/work/DBPFKit/DBPFKit/CMakeLists.txt;0;")
subdirs("_deps/libsquish-build")
subdirs("_deps/mio-build")
subdirs("_deps/raylib-build")
subdirs("vendor/catch2")
