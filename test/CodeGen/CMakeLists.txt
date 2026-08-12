# Codegen driven in process, which the end-to-end suites cannot do: these need
# to reach inside LangOptions and hand codegen a state the command line has no
# way to ask for.
add_executable(codegen_switches_test switches_test.cpp)
target_link_libraries(codegen_switches_test PRIVATE plang_frontend GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(codegen_switches_test)
