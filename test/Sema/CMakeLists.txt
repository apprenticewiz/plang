add_executable(sema_test sema_test.cpp)
target_include_directories(sema_test PRIVATE ${CMAKE_SOURCE_DIR}/test/Support)
target_link_libraries(sema_test PRIVATE plang_frontend GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(sema_test)
