add_executable(scanner_test scanner_test.cpp)
target_include_directories(scanner_test PRIVATE ${CMAKE_SOURCE_DIR}/test/Support)
target_link_libraries(scanner_test PRIVATE plang_frontend GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(scanner_test)
