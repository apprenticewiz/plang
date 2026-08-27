#!/usr/bin/env bash
# Fails loudly if the black-box lit suite silently lost coverage -- either
# the whole tree at once (test/CMakeLists.txt registers zero lit-* CTest
# entries when lit/FileCheck can't be found; PLANG_TESTS_REQUIRE_LIT turns
# that into a configure-time FATAL_ERROR, but only for the "tool is missing
# outright" case) or one category at a time (an add_plang_lit_suite() call
# deleted, a directory rename that orphans its .pas files from lit's own
# discovery, a lit.cfg.py suffix/exclude change that silently stops matching
# anything -- none of which touch LIT_EXECUTABLE/FILECHECK_EXECUTABLE at
# all, so PLANG_TESTS_REQUIRE_LIT cannot see them).
#
# issue #184: CI ran plain, unfiltered `ctest` and called that done. With
# lit/FileCheck missing, that command still exited 0, having quietly run
# only the ~51 GoogleTest cases and none of the ~2,000+ black-box ones.
# Green CI; the coverage was gone. This is the "did the tests we think we
# have actually get discovered" check, run right after Configure and before
# Build -- cheap (pure discovery: nothing is compiled or executed) and,
# unlike the FATAL_ERROR gate above, independent of *why* coverage shrank.
#
#   test/tools/assert-test-discovery.sh <build-dir> [min-suites] [min-tests]
#
# The suite list itself comes from `ctest -N`, not a second hardcoded copy
# of test/CMakeLists.txt's add_plang_lit_suite() calls, so it can never
# drift out of sync with that file. Only the two numeric floors below are
# hardcoded, both well under the real counts as of issue #184 (11 suites,
# 2,000+ tests), so ordinary test-suite growth never trips this -- raise
# them only if a whole category is deliberately retired.
set -u

build_dir=${1:-build}
min_suites=${2:-11}
min_tests=${3:-1500}

if [ ! -d "$build_dir" ]; then
    echo "assert-test-discovery.sh: no such build dir: $build_dir" >&2
    exit 2
fi

# lit-<Category> CTest wrapper entries actually registered at configure
# time. `ctest -N` needs no build -- CTestTestfile.cmake is written out by
# the configure step alone, so this can run before Build.
suite_list=$(ctest --test-dir "$build_dir" -N 2>/dev/null \
             | grep -oE ': lit-[A-Za-z0-9_]+' | sed 's/^: lit-//')
suite_count=0
if [ -n "$suite_list" ]; then
    suite_count=$(printf '%s\n' "$suite_list" | wc -l)
fi

echo "== lit-* CTest wrapper entries: $suite_count =="
if [ "$suite_count" -gt 0 ]; then
    printf '%s\n' "$suite_list" | sed 's/^/  lit-/'
fi

if [ "$suite_count" -lt "$min_suites" ]; then
    {
        echo "FAIL: only $suite_count lit-* CTest entries registered (expected >= $min_suites)."
        echo "This is issue #184's failure mode: lit/FileCheck not found, or a"
        echo "suite's add_plang_lit_suite() call missing, leaves CTest with"
        echo "nothing to fail on. Check the CMake configure log for a"
        echo "'lit/FileCheck not found' warning or error."
    } >&2
    exit 1
fi

# Individual tests each suite's lit.cfg.py actually discovers on disk --
# --show-tests only walks and matches files; it builds and runs nothing, so
# this is safe (and fast) to run before the Build step.
#
# Each suite is also checked for a hard zero on its own, not just folded into
# the aggregate floor below: a single category silently going to 0 (a
# directory rename, a filter/suffix change in lit.cfg.py) can easily still
# leave the total above min_tests, since no one category is most of the
# suite -- an aggregate-only check would miss exactly the kind of
# discovery/dependency regression this script exists to catch.
test_dir="$build_dir/test"
total=0
empty_suites=""
while IFS= read -r suite; do
    [ -n "$suite" ] || continue
    n=$(cd "$test_dir" && lit --show-tests -q "$suite" 2>/dev/null | grep -c '^  ')
    echo "  $suite: $n discovered tests"
    total=$((total + n))
    if [ "$n" -eq 0 ]; then
        empty_suites="$empty_suites $suite"
    fi
done <<< "$suite_list"

echo "== total discovered lit tests: $total =="

if [ -n "$empty_suites" ]; then
    echo "FAIL: these lit-* suites are registered but discovered zero tests:" >&2
    echo " $empty_suites" >&2
    echo "The directory is likely renamed/emptied, or lit.cfg.py's suffix/exclude" >&2
    echo "list no longer matches it." >&2
    exit 1
fi

if [ "$total" -lt "$min_tests" ]; then
    echo "FAIL: only $total individual lit tests discovered (expected >= $min_tests)." >&2
    exit 1
fi

echo "OK: $suite_count lit suites, $total discovered tests."
