find_package(GTest REQUIRED)

add_subdirectory(Basic)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
# The GoogleTest tier's black-box Driver tests migrated to test/Driver/ --
# see issue #34. Driver/ here is not a reversal of that: it holds one
# narrow, permanent exception for a Driver property with no CLI-observable
# proxy at all (issue #174) -- the same kind of exception
# test/unittests/Basic/catalog_test.cpp's own header comment documents for
# itself, not a return to testing Driver's ordinary CLI behavior here.
add_subdirectory(Driver)
# Conformance migrated to test/Conformance/ -- see issue #34.
# Acceptance migrated to test/Acceptance/ -- see issue #34.
# Lex migrated to test/Lex/ -- see issue #43.
# Parse migrated to test/Parse/ -- see issue #43.
