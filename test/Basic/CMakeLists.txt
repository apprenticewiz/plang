add_executable(catalog_test catalog_test.cpp)
target_link_libraries(catalog_test PRIVATE plang_frontend GTest::GTest GTest::Main)
# The generated catalogs live in the build tree; a test binary is its own main
# executable, so it has no bin/../share relation to find them by.
target_compile_definitions(catalog_test PRIVATE
    PLANG_CATALOG_DIR="${CMAKE_BINARY_DIR}/share/plang/locale")
add_dependencies(catalog_test plang_catalogs)

include(GoogleTest)
gtest_discover_tests(catalog_test)
