add_executable(driver_test driver_test.cpp)
target_link_libraries(driver_test PRIVATE PlangDriver GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(driver_test)
