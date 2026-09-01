# libFuzzer targets for Scanner, Parser, and .pmi loading (issue #183 Phase
# 1). Only ever added when PLANG_ENABLE_FUZZERS is ON, which the top-level
# CMakeLists.txt has already confirmed is Clang -- see that option's own
# comment for why -fsanitize=fuzzer is Clang-only. That same top-level block
# already applied "-fsanitize=fuzzer-no-link,address" project-wide (compile
# AND link options), so plang_frontend itself -- Scanner/Parser/Sema
# included -- is ASan- and SanitizerCoverage-instrumented by the time it
# reaches add_subdirectory(fuzz) here; only "-fsanitize=fuzzer" (link-only,
# no "-no-link") is added per target below, which is what actually pulls in
# libFuzzer's own main()/driver -- see the top-level block's comment for why
# that has to stay scoped to just these three targets.
#
# Each target links plang_frontend directly, the same way the GoogleTest
# tier's own executables do (test/unittests/*/CMakeLists.txt) -- Scanner,
# Parser and Sema are all bundled object files inside it (lib/CMakeLists.txt).

foreach(fuzz_target scanner-fuzzer parser-fuzzer pmi-fuzzer)
    add_executable(${fuzz_target} ${fuzz_target}.cpp)
    target_link_libraries(${fuzz_target} PRIVATE plang_frontend)
    target_link_options(${fuzz_target} PRIVATE -fsanitize=fuzzer)
endforeach()
