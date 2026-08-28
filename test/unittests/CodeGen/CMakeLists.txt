# Codegen driven in process, which the end-to-end suites cannot do: these need
# to reach inside LangOptions and hand codegen a state the command line has no
# way to ask for.
foreach(Suite switches storage)
    add_executable(codegen_${Suite}_test ${Suite}_test.cpp)
    target_link_libraries(codegen_${Suite}_test PRIVATE plang_frontend GTest::GTest GTest::Main)

    include(GoogleTest)
    gtest_discover_tests(codegen_${Suite}_test)
endforeach()

# checked_arith_test builds LLVM IR directly (RangeCheckGuards is exercised
# below its usual Codegen::Impl caller, at a Width the command line cannot
# reach yet -- see the file's own header comment), so unlike the two suites
# above it needs lib/CodeGen's own internal headers, not just plang/CodeGen/
# CodeGen.h, and needs LLVM linked directly rather than only transitively
# through plang_frontend (whose own LLVM dependency is PRIVATE, see lib/
# CMakeLists.txt's identical branch below for why).
add_executable(codegen_checked_arith_test checked_arith_test.cpp)
target_include_directories(codegen_checked_arith_test PRIVATE ${CMAKE_SOURCE_DIR}/lib/CodeGen)
target_link_libraries(codegen_checked_arith_test PRIVATE plang_frontend GTest::GTest GTest::Main)
if(TARGET LLVM)
    target_link_libraries(codegen_checked_arith_test PRIVATE LLVM)
else()
    target_link_libraries(codegen_checked_arith_test PRIVATE ${LLVM_SHARED_LIB})
endif()

include(GoogleTest)
gtest_discover_tests(codegen_checked_arith_test)
