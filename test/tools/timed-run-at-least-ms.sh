#!/usr/bin/env bash
# Runs a command and succeeds only if it took at least a given number of
# milliseconds -- Crt's own Delay(MS) is a real nanosleep-based wait
# (runtime/plang_crt.cpp's own plang_crt_delay), not a no-op, and this is
# what proves that from a lit RUN line.
#
#   test/tools/timed-run-at-least-ms.sh <min-ms> <cmd> [args...]
#
# A real, external script rather than an in-line RUN: sequence, for the same
# two reasons check-exit-code.sh's own comment (this directory) gives: lit's
# internal shell (test/lit.cfg.py: ShTest(execute_external=False)) has no
# real `$( )` command substitution or arithmetic either, and -- more
# specifically to timing -- any literal '%' in an in-line RUN: line (as
# `date +%s%N`'s own format string needs) is exactly what lit's own
# %s/%t/... substitution scans for and would rewrite before a shell ever
# saw it, not something `%%`-escaping could reliably route around inside a
# quoted command lit's own substitution runs BEFORE quoting is honored.
#
# On a mismatch, prints what actually elapsed so a failing test still says
# something useful; prints "elapsed_ms_ok=1" on success for a test to
# FileCheck if it wants that self-documenting a line rather than only
# relying on this script's own exit status.
set -u
min_ms="$1"; shift
start_ns=$(date +%s%N)
"$@"
rc=$?
stop_ns=$(date +%s%N)
elapsed_ms=$(( (stop_ns - start_ns) / 1000000 ))
if [ "$elapsed_ms" -lt "$min_ms" ]; then
    echo "timed-run-at-least-ms.sh: expected at least ${min_ms}ms, took ${elapsed_ms}ms" >&2
    exit 1
fi
if [ "$rc" -ne 0 ]; then
    echo "timed-run-at-least-ms.sh: command exited $rc" >&2
    exit "$rc"
fi
echo "elapsed_ms_ok=1"
exit 0
