find_package(GTest REQUIRED)

add_subdirectory(Basic)
add_subdirectory(Lex)
add_subdirectory(Parse)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
add_subdirectory(Driver)
add_subdirectory(Acceptance)
# Conformance migrated to test-lit/Conformance/ -- see issue #34.
