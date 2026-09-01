# In-process C++-API-level tests of plang::Driver properties with no
# CLI-observable proxy (issues #174 and #250) -- deliberately GoogleTest,
# not lit, for the same reason test/unittests/Basic/catalog_test.cpp's own
# header comment gives for its permanent GoogleTest cases.  See
# driver_test.cpp's own header comment for both.
add_executable(driver_test driver_test.cpp)
target_link_libraries(driver_test PRIVATE PlangDriver GTest::GTest GTest::Main)

# Issue #181: guards against Options.def listing a Frontend/Both option that
# frontendPC1Main's own hand-written -pc1 parser never grew an arm for -- see
# options_table_test.cpp's own header comment.  Links plang_frontend (the
# shared library frontendPC1Main is compiled into, same as the other
# unittests that call into the front end -- see test/unittests/Basic and
# test/unittests/Sema's own CMakeLists.txt) rather than PlangDriver, since
# Options.h itself is header-only and this test never touches the driver.
add_executable(options_table_test options_table_test.cpp)
target_link_libraries(options_table_test PRIVATE plang_frontend GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(driver_test)
gtest_discover_tests(options_table_test)
