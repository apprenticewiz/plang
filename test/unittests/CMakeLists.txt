find_package(GTest REQUIRED)

add_subdirectory(Basic)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
# Conformance migrated to test/Conformance/ -- see issue #34.
# Acceptance migrated to test/Acceptance/ -- see issue #34.
# Driver migrated to test/Driver/ -- see issue #34.
# Lex migrated to test/Lex/ -- see issue #43.
# Parse migrated to test/Parse/ -- see issue #43.
