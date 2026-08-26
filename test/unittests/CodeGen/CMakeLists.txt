# Codegen driven in process, which the end-to-end suites cannot do: these need
# to reach inside LangOptions and hand codegen a state the command line has no
# way to ask for.
foreach(Suite switches storage)
    add_executable(codegen_${Suite}_test ${Suite}_test.cpp)
    target_link_libraries(codegen_${Suite}_test PRIVATE plang_frontend GTest::GTest GTest::Main)

    include(GoogleTest)
    gtest_discover_tests(codegen_${Suite}_test)
endforeach()
