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
# --errors-for-leak-kinds=none keeps a leak out of the error count either
# way -- belt and suspenders, and see the -q paragraph below for why
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
# --log-file=<tmp file>, checked unconditionally below, independent of the
# wrapped program's own exit code: `--error-exitcode=1` alone is only
# visible to a RUN line that expects a CLEAN exit (a bare `%run %t`). A
# large, high-value slice of this suite's own RUN lines intentionally expect
# a NONZERO exit already -- `RUN: not %run %t ... | FileCheck ...`, the
# idiom every test of plang's own runtime-error/trap behavior uses. For
# those, Valgrind's own --error-exitcode=1 override is INVISIBLE: the
# program's intentional trap already exits nonzero, `not` already expects
# and inverts that, and a Valgrind-forced nonzero exit is indistinguishable
# there from the program's OWN expected nonzero trap -- confirmed for real
# while wiring the fix for this in: a synthetic program combining a genuine
# use-after-free with a legitimate `return 201;`-style trap passed its own
# `not %run %t` RUN line and its stderr FileCheck match cleanly, with
# Valgrind's own "Invalid read ... at plang_dispose/plang_new" finding
# completely invisible, silently buried in a `%t.err` nobody's RUN line ever
# printed. So a real Valgrind finding is instead decided by an unconditional
# post-hoc scan of Valgrind's OWN --log-file output -- entirely separate
# from the wrapped program's own stdout/stderr/exit code, so it is caught
# the same way regardless of which RUN-line shape wraps it. Given the flags
# above, that log file is empty UNLESS Valgrind reported a real (non-leak)
# error: -q suppresses the banner and (confirmed empirically) even the final
# "ERROR SUMMARY" line, --show-leak-kinds=none/--errors-for-leak-kinds=none
# together mean a leak alone never writes anything there either. So
# "non-empty log" and "real corruption finding" are equivalent under this
# exact flag set.
#
# When the log is non-empty, this process kills ITSELF with SIGABRT rather
# than merely returning a nonzero exit code. That is the one signal `not`
# (LLVM's own, matching this suite's own `not %run %t` idiom) does NOT
# invert: `not` treats a crash as an unconditional failure regardless of the
# `not` negation, unless invoked with `not --crash` (confirmed empirically:
# `not` on a self-SIGABRT'd child always reports failure, the same as it
# would for a real segfault, unlike a plain nonzero exit code which `not`
# happily inverts into a pass). lit's own internal shell (used for every
# RUN line in this suite, `not`-negated or not, since execute_external is
# off) independently treats a signal-terminated command's negative return
# code as a failing exit status too, so the same self-SIGABRT correctly
# fails a bare, non-`not` `%run %t` RUN line exactly as it always did (see
# run_under_valgrind's own comment below for the exact mechanics).
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

valgrind_flags=(-q --error-exitcode=1 --leak-check=full --show-leak-kinds=none --errors-for-leak-kinds=none)

# run_under_valgrind PROG [ARGS...]
#
# Runs PROG under valgrind and returns/reports an outcome lit and `not` can
# both trust, regardless of what PROG's own RUN line expects (a clean exit
# 0, or its own intentional nonzero trap via `not %run %t`):
#
#   - No Valgrind finding: returns PROG's own exit code unchanged, whatever
#     it is (0, or PROG's own intentional nonzero trap) -- identical to
#     this wrapper's behavior before this function existed.
#   - A real Valgrind finding: prints it (to this process's own stderr, so
#     it lands wherever the RUN line's own `2>` redirection already sends
#     it, and so it is visible in lit's captured output for the now-failing
#     test) and kills this process with SIGABRT -- see the header comment
#     above for why a signal, not just a different nonzero code, is
#     required to defeat `not`'s negation uniformly.
run_under_valgrind() {
    local log status
    log=$(mktemp "${TMPDIR:-/tmp}/plang-valgrind-log.XXXXXX") || return 2
    valgrind "${valgrind_flags[@]}" --log-file="$log" "$@"
    status=$?
    if [ -s "$log" ]; then
        {
            echo "run-under-valgrind.sh: Valgrind reported a real finding" \
                 "(independent of the wrapped program's own exit code $status)" \
                 "-- failing regardless of whether this RUN line expects a" \
                 "clean exit or its own intentional nonzero trap:"
            cat "$log"
        } >&2
        rm -f "$log"
        kill -ABRT "$$"
        # kill -ABRT on our own pid always terminates this process; nothing
        # below this line is expected to run. This is a fallback only, in
        # case SIGABRT was somehow blocked or ignored by the environment.
        exit 134
    fi
    rm -f "$log"
    return "$status"
}

# --self-test: does the gate still do what it claims? Same reasoning as
# run-under-guardheap.sh's own --self-test: a memory-safety gate that always
# says "clean" is indistinguishable from one that is simply broken (valgrind
# missing, a PATH problem, an option typo that silently disables error
# detection), so this checks multiple directions -- a valid program must
# stay clean (exit code AND stderr), a small heap overflow and a
# use-after-free must both still be caught, a plain leak must exit 0 with
# silent stderr (matching the --show-leak-kinds=none/
# --errors-for-leak-kinds=none choice above, and the exact regression that
# choice fixed for real against iso7185pat.pas), AND -- the regression this
# self-test exists to guard now -- a program combining a genuine
# use-after-free with its OWN legitimate nonzero "trap" exit (the exact
# `not %run %t` shape roughly three dozen files in this suite's own filtered
# scope use) must still be caught, not silently swallowed by `not`'s
# negation the way it was before --log-file/SIGABRT was added -- rather
# than trusting the exit code of a single unwrapped run.
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
    # trap_only: no memory bug at all, just an intentional nonzero "trap"
    # exit -- the common (unaffected) shape roughly three dozen files in
    # this suite's own filtered scope already use via `not %run %t`. Must
    # keep passing under `not` exactly as before: this is the regression
    # check for "did the fix start flagging a LEGITIMATE trap as a Valgrind
    # finding".
    cat > "$d/trap_only.c" <<'EOF'
#include <stdio.h>
int main(void){ fprintf(stderr, "runtime error 201\n"); return 201; }
EOF
    # uaf_and_trap: the reviewer's own synthetic scenario -- a GENUINE
    # use-after-free co-occurring with a legitimate `return 201;`-style trap
    # and its own expected stderr text, exactly the shape `not %run %t`
    # combined with a FileCheck of stderr was blind to before --log-file/
    # SIGABRT was added: the `not` check passed, the FileCheck substring
    # match still succeeded (buried in extra Valgrind noise on a plain
    # `--error-exitcode=1`-only wrapper), and the memory bug was invisible.
    cat > "$d/uaf_and_trap.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
int main(void){
    char *p = malloc(64);
    p[0] = 1;
    free(p);
    if (p[0] == 1) {
        fprintf(stderr, "runtime error 201\n");
        return 201;
    }
    return 0;
}
EOF
    cc -g -O0 -o "$d/ok"           "$d/ok.c"           || exit 2
    cc -g -O0 -o "$d/overflow"     "$d/overflow.c"     || exit 2
    cc -g -O0 -o "$d/uaf"          "$d/uaf.c"          || exit 2
    cc -g -O0 -o "$d/leak"         "$d/leak.c"         || exit 2
    cc -g -O0 -o "$d/trap_only"    "$d/trap_only.c"    || exit 2
    cc -g -O0 -o "$d/uaf_and_trap" "$d/uaf_and_trap.c" || exit 2

    # Each case re-invokes THIS SCRIPT as a fresh subprocess (exactly how
    # lit's own %run substitution invokes it for real: `bash
    # run-under-valgrind.sh %t`), not the run_under_valgrind function
    # in-process -- a self-SIGABRT is only safe to test from a separate
    # child, since $$ inside a bash function or `(...)` subshell still
    # names THIS self-test script's own pid, and killing that would abort
    # the self-test itself rather than reporting one case's outcome.
    run() { bash "$here/$(basename "${BASH_SOURCE[0]}")" "$1" >"$1.log" 2>&1; }

    run "$d/ok";           ok=$?
    run "$d/overflow";     overflow=$?
    run "$d/uaf";          uaf=$?
    run "$d/leak";         leak=$?
    run "$d/trap_only";    trap_only=$?
    run "$d/uaf_and_trap"; uaf_and_trap=$?

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
    # trap_only must exit nonzero (its OWN 201, untouched -- no Valgrind
    # finding to report) and stay silent (no Valgrind noise mixed into a
    # RUN line's own stderr capture).
    if [ "$trap_only" -ne 201 ]; then
        echo "valgrind gate regression: a program with its own legitimate nonzero trap and NO memory bug exited $trap_only, expected 201 unchanged" >&2
        cat "$d/trap_only.log" >&2
        fail=1
    fi
    if grep -q "run-under-valgrind.sh: Valgrind reported" "$d/trap_only.log" 2>/dev/null; then
        echo "valgrind gate FALSE POSITIVE: a legitimate trap with no memory bug was reported as a Valgrind finding" >&2
        cat "$d/trap_only.log" >&2
        fail=1
    fi
    # uaf_and_trap is the gap this self-test exists to close: a genuine
    # memory bug co-occurring with the program's own legitimate nonzero
    # trap must NOT be swallowed by that trap's own exit code -- the
    # wrapper must now report it via a signal (134 = 128+SIGABRT), not the
    # program's own "201".
    if [ "$uaf_and_trap" -eq 0 ] || [ "$uaf_and_trap" -eq 201 ]; then
        echo "valgrind gate FALSE NEGATIVE: a genuine use-after-free co-occurring with the program's own legitimate nonzero trap (exit $uaf_and_trap) was not caught -- this is the exact issue #561 review gap" >&2
        cat "$d/uaf_and_trap.log" >&2
        fail=1
    fi
    if ! grep -q "run-under-valgrind.sh: Valgrind reported" "$d/uaf_and_trap.log" 2>/dev/null; then
        echo "valgrind gate FALSE NEGATIVE: no Valgrind finding was reported for the use-after-free+trap case" >&2
        cat "$d/uaf_and_trap.log" >&2
        fail=1
    fi

    # Best-effort end-to-end proof against the REAL `not` tool this suite's
    # own `not %run %t` RUN lines use, mirroring the reviewer's exact
    # synthetic scenario: before this fix, `not <wrapper> uaf_and_trap`
    # reported PASS (uaf_and_trap's own nonzero 201 was all `not` ever saw).
    # Optional (not a hard failure) since --self-test itself is invoked
    # directly by a bare `bash run-under-valgrind.sh --self-test`, outside
    # of lit's own PATH setup, so `not` is not guaranteed to be resolvable
    # here the way it is once the real lit suite runs.
    if command -v not >/dev/null 2>&1; then
        # `not CMD` exits 0 ("pass") exactly when CMD's own exit code is
        # nonzero and CMD did NOT crash -- i.e. when `not` judges the RUN
        # line to have gotten the trap it expected. For uaf_and_trap we
        # want the OPPOSITE: a real Valgrind finding must make `not` report
        # FAILURE (nonzero) despite the program's own nonzero 201, so the
        # "fix works" case is the `else` branch here, not the `then`.
        if not bash "$here/$(basename "${BASH_SOURCE[0]}")" "$d/uaf_and_trap" >"$d/uaf_and_trap.not.log" 2>&1; then
            echo "valgrind gate FALSE NEGATIVE: 'not run-under-valgrind.sh uaf_and_trap' reported PASS -- the exact issue #561 review gap ('not %run %t' hides a real Valgrind finding behind the program's own expected nonzero trap)" >&2
            cat "$d/uaf_and_trap.not.log" >&2
            fail=1
        fi
        if ! not bash "$here/$(basename "${BASH_SOURCE[0]}")" "$d/trap_only" >"$d/trap_only.not.log" 2>&1; then
            echo "valgrind gate regression: 'not run-under-valgrind.sh trap_only' reported FAIL for a legitimate trap with no memory bug" >&2
            cat "$d/trap_only.not.log" >&2
            fail=1
        fi
    else
        echo "valgrind gate self-test: 'not' not found on PATH, skipping the end-to-end 'not %run %t' proof (covered indirectly via the raw exit-code/signal checks above)" >&2
    fi

    [ "$fail" -eq 0 ] && echo "valgrind gate self-test: valid program silent+clean, overflow/use-after-free/use-after-free-under-a-trap all caught, plain leak and a legitimate trap alone both exit unchanged and stay silent"
    exit "$fail"
fi

run_under_valgrind "$@"
exit $?
