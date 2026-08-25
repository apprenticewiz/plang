# One executable now.  This was a single driver_test.cpp of eleven thousand
# lines, which compiled serially however many cores ctest was given, and
# split by what a case is about, not by what it calls -- DriverHarness.h is
# what they all shared. codegen, ep, and module have all migrated to
# test-lit/{CodeGen,EP,Module}/; driver_test.cpp is deliberately last (issue
# #34's hardest content -- DWARF debug-info scope-graph assertions).
foreach(Suite driver)
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
