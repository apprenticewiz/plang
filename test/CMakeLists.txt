find_package(GTest REQUIRED)

add_subdirectory(Basic)
add_subdirectory(Lex)
add_subdirectory(Parse)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
# Conformance migrated to test-lit/Conformance/ -- see issue #34.
# Acceptance migrated to test-lit/Acceptance/ -- see issue #34.
# Driver migrated to test-lit/Driver/ -- see issue #34.
