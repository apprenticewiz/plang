add_executable(parser_test parser_test.cpp)
target_include_directories(parser_test PRIVATE ${CMAKE_SOURCE_DIR}/test/Support)
target_link_libraries(parser_test PRIVATE plang_frontend GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(parser_test)
