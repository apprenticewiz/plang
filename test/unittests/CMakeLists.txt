find_package(GTest REQUIRED)

add_subdirectory(Basic)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
add_subdirectory(Driver)
# Conformance migrated to test/Conformance/ -- see issue #34.
# Acceptance migrated to test/Acceptance/ -- see issue #34.
# The rest of the pre-issue-#34 Driver GTest suite migrated to test/Driver/;
# Driver/driver_test.cpp (issue #250) is a new, narrowly-scoped exception --
# see its file header for why.
# Lex migrated to test/Lex/ -- see issue #43.
# Parse migrated to test/Parse/ -- see issue #43.
