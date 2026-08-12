# Four executables, not one.  These were a single driver_test.cpp of eleven
# thousand lines, which compiled serially however many cores ctest was given
# and left the next suite -- Turbo Pascal's -- with a choice between adding to
# it and copying it.  DriverHarness.h is what they share; the split is by what
# a case is about, not by what it calls.
foreach(Suite driver codegen ep module)
    add_executable(${Suite}_test ${Suite}_test.cpp)
    target_include_directories(${Suite}_test PRIVATE
        ${CMAKE_SOURCE_DIR}/test/Support
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${Suite}_test PRIVATE GTest::GTest GTest::Main)
    target_compile_definitions(${Suite}_test PRIVATE
        PLANG_PATH="$<TARGET_FILE:plang>"
    )
    add_dependencies(${Suite}_test plang)

    include(GoogleTest)
    gtest_discover_tests(${Suite}_test)
endforeach()
