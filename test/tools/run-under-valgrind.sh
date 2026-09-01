#!/usr/bin/env bash
# Run a plang-compiled program under Valgrind's memcheck tool.
#
# Why this exists (issue #190 part B option 3): the guard-page allocator in
# this same directory (run-under-guardheap.sh) catches a write that walks off
# the END of a heap allocation IF it reaches all the way to the next page
# boundary, but nothing else in this tree can see a use-after-free, a
# double-free, an uninitialized-value read, or a SMALL heap overflow that
# never reaches guardheap's own guard page. Confirmed for real (not just
# claimed) against small standalone repro programs while wiring this in:
# memcheck's own per-allocation redzones caught a 1-byte overflow one byte
# past a 64-byte malloc with no guard page involved at all, and a dereference
# of an already-`dispose`d pointer was flagged as an invalid read of freed
# memory -- and that freed byte was still readable, printing the ORIGINAL
# value and exiting 0 on a plain unwrapped run, the same "silent corruption"
# shape that motivated guardheap in the first place.
#
#   test/tools/run-under-valgrind.sh ./myprog [args...]
#
# --leak-check=full --show-leak-kinds=none --errors-for-leak-kinds=none:
# this gate targets memory CORRUPTION (invalid reads/writes, use-after-free,
# double-free, uninitialized-value use), not leak hygiene. --leak-check=full
# is still on (matching this project's own issue #190 triage comment, which
# named this exact invocation) so memcheck still DOES the leak search, but
# --show-leak-kinds=none stops it printing any leak record and
# --errors-for-leak-kinds=none keeps a leak out of the error count/exit code
# either way -- belt and suspenders, and see the -q paragraph below for why
# --show-leak-kinds=none specifically is not optional. Turned up for real
# while wiring this in: the ISO 7185 acceptance program
# (test/Acceptance/iso7185pat.pas), a large, carefully-constructed
# conformance test nobody has ever audited for dispose-everything
# cleanliness, definitely-leaks 25 bytes in 4 blocks (filed as issue #560,
# not fixed here). That is not a corruption bug and fixing decades-old
# acceptance-test hygiene is a separate piece of work from standing up this
# gate -- see docs/technical_info.md's "Valgrind memcheck (scheduled)"
# section for the full reasoning.
#
# -q/--quiet: valgrind's own startup banner normally goes to stderr
# UNCONDITIONALLY, on every run, clean or not. A handful of tests in this
# suite (e.g. Acceptance/iso7185pat.pas's own `test ! -s %t.err` RUN line)
# assert the wrapped program's stderr is EMPTY on a passing run -- true of
# the plain, unwrapped program, but broken by valgrind's own banner alone
# even with zero memcheck errors. -q suppresses that banner on a clean run.
# It alone is NOT enough, though: confirmed empirically while wiring this in
# that --leak-check=full still prints each individual "N bytes ... definitely
# lost in loss record ..." block to stderr even under -q and even with
# --errors-for-leak-kinds=none (that flag only controls what counts as an
# ERROR, not what gets PRINTED) -- iso7185pat.pas's own known leak (see
# above) kept failing its empty-stderr RUN line on exit code 0 until
# --show-leak-kinds=none was added alongside it. With all three together, a
# clean-of-corruption run (leaky or not) produces zero bytes of stderr, and
# an actual corruption finding still prints in full, banner included.
#
# Exit 1 means memcheck reported at least one non-leak error (via
# --error-exitcode=1, so a program that would itself have exited 0 still
# fails here); a fatal signal from the program itself still reports as that
# signal, exactly like an unwrapped run.
set -u

# --self-test: does the gate still do what it claims? Same reasoning as
# run-under-guardheap.sh's own --self-test: a memory-safety gate that always
# says "clean" is indistinguishable from one that is simply broken (valgrind
# missing, a PATH problem, an option typo that silently disables error
# detection), so this checks multiple directions -- a valid program must
# stay clean (exit code AND stderr), a small heap overflow and a
# use-after-free must both still be caught, and a plain leak must exit 0
# with silent stderr (matching the --show-leak-kinds=none/
# --errors-for-leak-kinds=none choice above, and the exact regression that
# choice fixed for real against iso7185pat.pas) -- rather than trusting the
# exit code of a single run.
if [ "${1:-}" = "--self-test" ]; then
    d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
    cat > "$d/ok.c" <<'EOF'
#include <stdlib.h>
int main(void){ void *p = malloc(64); ((char*)p)[0] = 1; free(p); return 0; }
EOF
    cat > "$d/overflow.c" <<'EOF'
#include <stdlib.h>
int main(void){ char *p = malloc(64); p[64] = 1; free(p); return 0; }
EOF
    cat > "$d/uaf.c" <<'EOF'
#include <stdlib.h>
int main(void){
    char *p = malloc(64);
    p[0] = 1;
    free(p);
    return p[0];
}
EOF
    cat > "$d/leak.c" <<'EOF'
#include <stdlib.h>
int main(void){ void *p = malloc(64); ((char*)p)[0] = 1; return 0; }
EOF
    cc -g -O0 -o "$d/ok"       "$d/ok.c"       || exit 2
    cc -g -O0 -o "$d/overflow" "$d/overflow.c" || exit 2
    cc -g -O0 -o "$d/uaf"      "$d/uaf.c"      || exit 2
    cc -g -O0 -o "$d/leak"     "$d/leak.c"     || exit 2

    run() { valgrind -q --error-exitcode=1 --leak-check=full --show-leak-kinds=none --errors-for-leak-kinds=none "$1" >"$1.log" 2>&1; }

    run "$d/ok";       ok=$?
    run "$d/overflow";  overflow=$?
    run "$d/uaf";       uaf=$?
    run "$d/leak";      leak=$?

    fail=0
    if [ "$ok" -ne 0 ]; then
        echo "valgrind gate FALSE POSITIVE: a valid program exited $ok" >&2
        cat "$d/ok.log" >&2
        fail=1
    fi
    if [ -s "$d/ok.log" ]; then
        echo "valgrind gate FALSE POSITIVE: a valid program produced output (should be silent under -q)" >&2
        cat "$d/ok.log" >&2
        fail=1
    fi
    if [ "$overflow" -eq 0 ]; then
        echo "valgrind gate FALSE NEGATIVE: a 1-byte heap overflow exited 0" >&2
        fail=1
    fi
    if [ "$uaf" -eq 0 ]; then
        echo "valgrind gate FALSE NEGATIVE: a use-after-free exited 0" >&2
        fail=1
    fi
    if [ "$leak" -ne 0 ]; then
        echo "valgrind gate over-eager: a plain leak (no corruption) exited $leak, expected 0 -- errors-for-leak-kinds is no longer 'none'?" >&2
        fail=1
    fi
    if [ -s "$d/leak.log" ]; then
        echo "valgrind gate over-eager: a plain leak (no corruption) produced output (should be silent under --show-leak-kinds=none) -- this is exactly the regression that broke Acceptance/iso7185pat.pas's own empty-stderr RUN line" >&2
        cat "$d/leak.log" >&2
        fail=1
    fi
    [ "$fail" -eq 0 ] && echo "valgrind gate self-test: valid program silent+clean, overflow and use-after-free caught, plain leak exits 0 and stays silent"
    exit "$fail"
fi

exec valgrind -q --error-exitcode=1 --leak-check=full --show-leak-kinds=none --errors-for-leak-kinds=none "$@"
