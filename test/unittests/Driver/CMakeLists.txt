# In-process C++-API-level tests of plang::Driver properties with no
# CLI-observable proxy (issues #174 and #250) -- deliberately GoogleTest,
# not lit, for the same reason test/unittests/Basic/catalog_test.cpp's own
# header comment gives for its permanent GoogleTest cases.  See
# driver_test.cpp's own header comment for both.
add_executable(driver_test driver_test.cpp)
target_link_libraries(driver_test PRIVATE PlangDriver GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(driver_test)
