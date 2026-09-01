# Issue #179: exercises plang::compile() (plang/Frontend/Compilation.h)
# directly -- the same plang_frontend shared library the other unittests
# under test/unittests/**'s own CMakeLists.txt link against (see
# test/unittests/Sema/CMakeLists.txt's own comment).
add_executable(frontend_test frontend_test.cpp)
target_link_libraries(frontend_test PRIVATE plang_frontend GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(frontend_test)
