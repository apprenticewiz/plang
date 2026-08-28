#!/usr/bin/env bash
# Runs a command and succeeds only if its exit status equals a given number.
#
#   test/tools/check-exit-code.sh <expected-code> <cmd> [args...]
#
# lit's own internal shell (test/lit.cfg.py: ShTest(execute_external=False))
# has no real `$?`: a RUN line like `%run %t; echo $?`, the everyday shell
# idiom for this, reaches lit's own glob-token parser instead of a POSIX
# shell and fails outright with a Python TypeError building a GlobItem --
# confirmed empirically while adding this script, not assumed. There is
# also no `not --exit-code=<n>` or equivalent built into lit's `not` --
# only "exited zero" vs. "exited nonzero" (plain `not`) or "died by
# signal" (`not --crash`), neither of which tells 200 apart from 201.  So,
# same as run-with-stdin-held-open.sh and kill-during-compile.sh (this
# directory) for job control lit's internal shell cannot do either: a
# real, external script.
#
# Prints what it actually got on a mismatch, so a failing test still says
# something useful rather than just "command failed" -- FileCheck can
# match this same output if a test wants to pin the number rather than
# only rely on this script's own exit status.
set -u
expected="$1"; shift
"$@"
actual=$?
if [ "$actual" -ne "$expected" ]; then
    echo "check-exit-code.sh: expected exit $expected, got $actual" >&2
    exit 1
fi
exit 0
