# In-process C++-API-level test of plang::Driver's own contract (issue
# #174) -- deliberately GoogleTest, not lit, for the same reason
# test/unittests/Basic/catalog_test.cpp's own header comment gives for its
# permanent GoogleTest cases: the property under test (does Driver::run()
# return control to its caller, or does it terminate the process out from
# under it) has no CLI-observable proxy at all.  Spawning the compiled
# `plang` binary, the way every test/Driver/ lit case does, sees the same
# exit code and the same stdout/stderr either way -- the only way to tell
# the two apart is to call Driver::run() in the calling process itself.
add_executable(driver_test driver_test.cpp)
target_link_libraries(driver_test PRIVATE PlangDriver GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(driver_test)
