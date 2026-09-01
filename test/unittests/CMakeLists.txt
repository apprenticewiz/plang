find_package(GTest REQUIRED)

add_subdirectory(Basic)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
# The GoogleTest tier's black-box Driver tests migrated to test/Driver/ --
# see issue #34. Driver/ here is not a reversal of that: it holds two
# narrow, permanent exceptions for Driver properties with no CLI-observable
# proxy at all (issues #174 and #250) -- the same kind of exception
# test/unittests/Basic/catalog_test.cpp's own header comment documents for
# itself, not a return to testing Driver's ordinary CLI behavior here.
add_subdirectory(Driver)
# Conformance migrated to test/Conformance/ -- see issue #34.
# Acceptance migrated to test/Acceptance/ -- see issue #34.
# Lex migrated to test/Lex/ -- see issue #43.
# Parse migrated to test/Parse/ -- see issue #43.

# Issue #190 part B option 1: only added when PLANG_ENABLE_RUNTIME_SANITIZER_TESTS
# is ON, since it links plang_runtime_sanitized (runtime/CMakeLists.txt),
# which only exists under that same option -- the top-level CMakeLists.txt's
# own configure-time check already guarantees PLANG_ENABLE_TESTS is ON
# whenever PLANG_ENABLE_RUNTIME_SANITIZER_TESTS is, so this line is always
# reached in that case.
if(PLANG_ENABLE_RUNTIME_SANITIZER_TESTS)
    add_subdirectory(RuntimeSanitized)
endif()
