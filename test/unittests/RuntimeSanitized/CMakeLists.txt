# Issue #190 part B option 1 -- a runtime-exerciser harness linking directly
# against plang_runtime_sanitized (runtime/CMakeLists.txt's own
# PLANG_ENABLE_RUNTIME_SANITIZER_TESTS block), never plang_runtime or
# plang_runtime_shared. Only ever reached when PLANG_ENABLE_RUNTIME_SANITIZER_TESTS
# is ON (see test/unittests/CMakeLists.txt's own guard), so plang_runtime_sanitized
# is guaranteed to already exist here.
#
# One executable, two source files -- mirrors test/unittests/Basic's own
# shape (catalog_test.cpp, source_manager_test.cpp) except combined into a
# single binary rather than two, since both files link the identical single
# library and this way CI only needs one `--target`/`ctest -R` to reach all
# of it.
add_executable(runtime_sanitized_test
    strings_sanitized_test.cpp
    file_sanitized_test.cpp
)
# plang/Basic/PascalFileLayout.h and plang/Basic/RequiredRecordLayouts.h --
# the one shared C++ encoding of PascalFile/PlangBindingType that codegen
# also reads (see those headers' own comments) -- are what let this file
# build a real PascalFile/PlangBindingType directly, with no Sema/CodeGen
# involved at all.
target_include_directories(runtime_sanitized_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(runtime_sanitized_test PRIVATE
    plang_runtime_sanitized GTest::GTest GTest::Main)

include(GoogleTest)
gtest_discover_tests(runtime_sanitized_test)
