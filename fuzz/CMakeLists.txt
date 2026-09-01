# libFuzzer targets for Scanner, Parser, and .pmi loading (issue #183 Phase
# 1). Only ever added when PLANG_ENABLE_FUZZERS is ON, which the top-level
# CMakeLists.txt has already confirmed is Clang -- see that option's own
# comment for why -fsanitize=fuzzer is Clang-only, and for why this whole
# file recompiles the frontend subsystems below into their own, separate,
# sanitized object libraries instead of reusing plang_frontend (or the
# PlangBasic/PlangAST/PlangLex/PlangParse/PlangSema targets it bundles)
# directly: instrumenting those broke every consumer that already links
# them -- the plang driver, plang_runtime, and every GoogleTest unittest
# under test/unittests/ -- the moment PLANG_ENABLE_TESTS=ON and
# PLANG_ENABLE_FUZZERS=ON were combined in one configure.
#
# Source lists for each Fuzz-suffixed library below are read straight from
# the ORIGINAL target's own SOURCES property (plang_fuzz_recompile, right
# below), not hand-copied, so they cannot silently drift out of sync with
# lib/*/CMakeLists.txt if a source file is ever added, renamed, or removed
# there. The dependency EDGES between the Fuzz variants (the
# target_link_libraries calls right after the five plang_fuzz_recompile
# calls) mirror lib/Lex, lib/Parse and lib/Sema's own -- that part IS hand-
# maintained, since CMake has no target property to read an existing
# target's PRIVATE target_link_libraries edges back out; it would need a
# matching update if those files' own edges ever change.
function(plang_fuzz_recompile new_target original_target)
    get_target_property(_plang_fuzz_srcs   ${original_target} SOURCES)
    get_target_property(_plang_fuzz_srcdir ${original_target} SOURCE_DIR)
    set(_plang_fuzz_abs_srcs)
    foreach(_plang_fuzz_src ${_plang_fuzz_srcs})
        if(IS_ABSOLUTE "${_plang_fuzz_src}")
            list(APPEND _plang_fuzz_abs_srcs "${_plang_fuzz_src}")
        else()
            list(APPEND _plang_fuzz_abs_srcs "${_plang_fuzz_srcdir}/${_plang_fuzz_src}")
        endif()
    endforeach()
    add_library(${new_target} OBJECT ${_plang_fuzz_abs_srcs})
    target_link_libraries(${new_target} PRIVATE plang_warnings)
    # -fsanitize=fuzzer-no-link (not plain "-fsanitize=fuzzer"): adds
    # libFuzzer's SanitizerCoverage instrumentation -- real coverage-guided
    # feedback out of the actual Scanner/Parser/Sema code being fuzzed, not
    # just the handful of lines each fuzz/*.cpp entry point below adds on
    # top -- without also pulling in libFuzzer's own main(), which belongs
    # only on the three executables at the bottom of this file.
    target_compile_options(${new_target} PRIVATE
        -fno-rtti -fno-exceptions -fPIC
        -fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer -g)
    # Compile definitions (PLANG_CATALOG_DIR, PLANG_UNIT_DIR, ...), if the
    # original target set any -- see lib/Basic/CMakeLists.txt (PlangLocale,
    # PlangBasic) for what these are and why.
    get_target_property(_plang_fuzz_defs ${original_target} COMPILE_DEFINITIONS)
    if(_plang_fuzz_defs)
        target_compile_definitions(${new_target} PRIVATE ${_plang_fuzz_defs})
    endif()
endfunction()

plang_fuzz_recompile(PlangLocaleFuzz PlangLocale)
plang_fuzz_recompile(PlangBasicFuzz  PlangBasic)
plang_fuzz_recompile(PlangASTFuzz    PlangAST)
plang_fuzz_recompile(PlangLexFuzz    PlangLex)
plang_fuzz_recompile(PlangParseFuzz  PlangParse)
plang_fuzz_recompile(PlangSemaFuzz   PlangSema)

# PlangSemaFuzz alone additionally needs PLANG_ENABLE_FUZZERS defined:
# Sema.h's fuzzLoadPMIFromBuffer hook (see its own comment) is gated on
# exactly that macro. Defined only for this fuzz-only recompilation, never
# for the real PlangSema build (lib/Sema/CMakeLists.txt), so an ordinary
# (non-fuzz) Sema never carries the extra public method at all.
target_compile_definitions(PlangSemaFuzz PRIVATE PLANG_ENABLE_FUZZERS=1)

# Mirrors lib/Lex/CMakeLists.txt, lib/Parse/CMakeLists.txt and
# lib/Sema/CMakeLists.txt's own target_link_libraries edges -- see this
# file's own header comment on why these lines are hand-maintained rather
# than auto-derived the way the source lists above are.
target_link_libraries(PlangLexFuzz   PRIVATE PlangBasicFuzz PlangLocaleFuzz)
target_link_libraries(PlangParseFuzz PRIVATE PlangLexFuzz PlangLocaleFuzz)
target_link_libraries(PlangSemaFuzz  PRIVATE PlangLexFuzz PlangParseFuzz PlangLocaleFuzz)

# The three libFuzzer entry points themselves. Each links only the Fuzz
# libraries it actually needs (PlangASTFuzz is deliberately unused by any of
# them -- AstPrinter.cpp is dump/debug-output code none of the three
# targets' own code paths reach -- so it is not linked into any of them).
add_executable(scanner-fuzzer scanner-fuzzer.cpp)
target_link_libraries(scanner-fuzzer PRIVATE
    PlangLexFuzz PlangBasicFuzz PlangLocaleFuzz)

add_executable(parser-fuzzer parser-fuzzer.cpp)
target_link_libraries(parser-fuzzer PRIVATE
    PlangParseFuzz PlangLexFuzz PlangBasicFuzz PlangLocaleFuzz)

add_executable(pmi-fuzzer pmi-fuzzer.cpp)
target_link_libraries(pmi-fuzzer PRIVATE
    PlangSemaFuzz PlangParseFuzz PlangLexFuzz PlangBasicFuzz PlangLocaleFuzz)
# Unlike scanner-fuzzer/parser-fuzzer, Sema.cpp itself calls a handful of
# out-of-line LLVM Support functions directly (llvm::sys::fs::status/
# directory_iterator for warnIfInterfaceStale's mtime check,
# llvm::report_fatal_error, SmallVector's own out-of-line growth code, ...).
# The real PlangSema never links LLVM itself either (lib/Sema/CMakeLists.txt
# has no such edge) -- it relies on plang_frontend's own final link
# supplying libLLVM.so, which pmi-fuzzer has no equivalent of, so it needs
# this edge directly. Same TARGET-then-find_library fallback
# lib/Driver/CMakeLists.txt already uses for the identical need.
if(TARGET LLVM)
    target_link_libraries(pmi-fuzzer PRIVATE LLVM)
else()
    find_library(LLVM_SHARED_LIB NAMES LLVM LLVM-${LLVM_PACKAGE_VERSION} REQUIRED)
    target_link_libraries(pmi-fuzzer PRIVATE ${LLVM_SHARED_LIB})
endif()
# pmi-fuzzer.cpp itself calls Sema::fuzzLoadPMIFromBuffer, so its own
# translation unit needs to see the SAME (fuzz-enabled) definition of Sema
# that PlangSemaFuzz's own compilation of Sema.cpp/Sema.h just above does --
# both are part of this one, entirely self-contained fuzz-only build, never
# linked against the real (non-fuzz) PlangSema, so there is no ODR concern
# in the two disagreeing.
target_compile_definitions(pmi-fuzzer PRIVATE PLANG_ENABLE_FUZZERS=1)

foreach(fuzz_target scanner-fuzzer parser-fuzzer pmi-fuzzer)
    target_compile_options(${fuzz_target} PRIVATE
        -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)
    # Plain "-fsanitize=fuzzer" (not "-no-link"): this is what actually
    # pulls in libFuzzer's own main()/driver for these three targets alone.
    target_link_options(${fuzz_target} PRIVATE -fsanitize=fuzzer,address)
endforeach()
