# lit-based tests (issue #34). A sibling of test/, not a replacement -- see
# test-lit/README.md.  Only reached when PLANG_ENABLE_TESTS=ON.

find_program(LIT_EXECUTABLE NAMES lit llvm-lit
             HINTS ${LLVM_TOOLS_BINARY_DIR})
find_program(FILECHECK_EXECUTABLE NAMES FileCheck
             HINTS ${LLVM_TOOLS_BINARY_DIR})
find_program(NOT_EXECUTABLE NAMES not
             HINTS ${LLVM_TOOLS_BINARY_DIR})
find_program(SPLIT_FILE_EXECUTABLE NAMES split-file
             HINTS ${LLVM_TOOLS_BINARY_DIR})

# WARNING, not FATAL_ERROR: PLANG_ENABLE_TESTS is already opt-in specifically
# so building the compiler itself never forces a test dependency on anyone --
# a developer who enabled it for the GoogleTest tier alone should not be
# blocked by one lit tool's absence on their machine.  Not every LLVM
# packaging bundles lit/FileCheck with the C++ tools the way this project's
# own dev machine's does; see issue #34.
if(NOT LIT_EXECUTABLE OR NOT FILECHECK_EXECUTABLE)
    message(WARNING
        "lit/FileCheck not found (looked in ${LLVM_TOOLS_BINARY_DIR} and "
        "PATH) -- the check-lit-* targets and their ctest entries will not "
        "be available. Try 'pip install lit' and re-configure, or pass "
        "-DLIT_EXECUTABLE=/path/to/lit directly.")
    return()
endif()

set(PLANG_SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(PLANG_BINARY_DIR ${CMAKE_BINARY_DIR})
set(PLANG_TOOLS_DIR  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
    ${CMAKE_BINARY_DIR}/test-lit/lit.site.cfg.py
    @ONLY
)

set(LIT_ARGS -sv)

# One CTest entry and one `ninja check-lit-<category>` target per top-level
# lit directory (LLVM-idiomatic granularity, not one per test file -- see
# issue #34). Ad-hoc single-test iteration during development is
# `lit --filter=<regex> build/test-lit/<Category>`, not `ctest -R`.
function(add_plang_lit_suite category)
    add_custom_target(check-lit-${category}
        COMMAND ${LIT_EXECUTABLE} ${LIT_ARGS}
                ${CMAKE_BINARY_DIR}/test-lit/${category}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test-lit
        DEPENDS plang
        COMMENT "Running lit tests: ${category}"
        USES_TERMINAL
    )
    add_test(
        NAME lit-${category}
        COMMAND ${LIT_EXECUTABLE} ${LIT_ARGS}
                ${CMAKE_BINARY_DIR}/test-lit/${category}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test-lit
    )
    add_dependencies(check-lit check-lit-${category})
endfunction()

add_custom_target(check-lit
    COMMENT "Running all lit-based test suites"
)

# Real categories are added one add_plang_lit_suite() line per migration
# phase (issue #34), in the same commit that adds that phase's
# test-lit/<Category>/ content.
add_plang_lit_suite(Smoke)
add_plang_lit_suite(Conformance)
add_plang_lit_suite(Acceptance)
add_plang_lit_suite(CodeGen)
add_plang_lit_suite(EP)
add_plang_lit_suite(Module)
add_plang_lit_suite(Driver)
add_plang_lit_suite(Lex)
add_plang_lit_suite(Parse)
add_plang_lit_suite(Sema)
add_plang_lit_suite(Basic)
# Basic/Catalog exercises the real shipped locale catalogs (fr.po, es_MX.po,
# qps_ploc.po, ...) via -fdiagnostics-language=, which live in the build
# tree only once plang_catalogs (an `ALL`-target, so a full build already
# includes it) has actually run -- unlike every other suite here, which only
# needs the plang binary itself. Without this, `ninja check-lit-Basic` run on
# its own, the same one-suite dev shortcut every other add_plang_lit_suite()
# call already supports, fails on the catalogs that were never generated.
add_dependencies(check-lit-Basic plang_catalogs)
