# The Pascal Acceptance Test — one whole ISO 7185 program, run and compared
# against audited output.  See acceptance_test.cpp for what the expected file
# is and how it was checked.

add_executable(acceptance_test acceptance_test.cpp)
target_link_libraries(acceptance_test PRIVATE GTest::GTest GTest::Main)
target_compile_definitions(acceptance_test PRIVATE
    PLANG_PATH="$<TARGET_FILE:plang>"
    ACCEPTANCE_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cases"
)
add_dependencies(acceptance_test plang)

include(GoogleTest)
gtest_discover_tests(acceptance_test)
