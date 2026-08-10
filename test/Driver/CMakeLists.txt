add_executable(driver_test driver_test.cpp)
target_include_directories(driver_test PRIVATE ${CMAKE_SOURCE_DIR}/test/Support)
target_link_libraries(driver_test PRIVATE GTest::GTest GTest::Main)
target_compile_definitions(driver_test PRIVATE
    PLANG_PATH="$<TARGET_FILE:plang>"
)
add_dependencies(driver_test plang)

include(GoogleTest)
gtest_discover_tests(driver_test)
