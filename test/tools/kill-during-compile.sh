#!/usr/bin/env bash
# Runs a command with TMPDIR pointed at a scratch directory, sends the
# whole command's process group SIGTERM as soon as something appears in
# that scratch directory, and reports what happened -- exercising the
# driver's interrupted-mid-compile temp-file cleanup (issue #278).
#
#   test/tools/kill-during-compile.sh <scratch-dir> <cmd> [args...]
#
# Prints CAUGHT-MID-FLIGHT if the command was still alive (and so was
# actually interrupted, not just fast) when the signal was sent, or
# FINISHED-BEFORE-SIGNAL if it had already exited on its own first --
# the latter means nothing about issue #278 was actually exercised, so a
# caller should treat it as inconclusive rather than as a pass. Then
# prints SCRATCH-DIR-CLEAN or SCRATCH-DIR-HAS-LEFTOVERS for the scratch
# directory's contents once the command has exited -- deliberately not a
# plain EMPTY/NOT-EMPTY pair, since "NOT-EMPTY" contains "EMPTY" as a
# substring and FileCheck's default matching is substring, not full-line;
# a CHECK-NEXT: EMPTY would have matched either outcome.
#
# `set -m` (job control) so the backgrounded command starts its own
# process group (pgid == its own pid) rather than sharing this script's --
# plang's own children (the -pc1 front end it re-invokes itself as, and
# llc) inherit that group by fork, so signaling the whole *group*
# (kill -TERM -- -pid) reaches them too. Signaling only the driver's own
# pid, tried first, left its child running orphaned after the driver died
# -- still writing, or about to open, the very file being watched here --
# which then kept surviving into the next trial of a tight retry loop and
# piling up (not a bug in the fix being tested, just this script racing
# its own leftovers). A real terminal's Ctrl-C reaches a whole foreground
# process group the same way, so this is the more faithful reproduction of
# the issue's own repro ("kill -TERM the driver") too, not just a
# workaround for the test.
#
# A plain FOREGROUND polling loop watching for the scratch directory to
# stop being empty, not a fixed sleep-then-kill: this reacts the moment
# there is something to clean up, whatever the command, rather than tuning
# a delay to some particular compile's wall-clock time. See
# run-with-stdin-held-open.sh for why a *backgrounded* watchdog sleep is
# avoided instead (it can leak a process that outlives this script).
set -u
set -m
tmp="$1"; shift
# Cleared, not just created: lit reuses the same %t.dir across separate
# `lit` invocations (split-file re-extracts its own known files on each run
# but does not clear anything else already there), so a leftover from an
# earlier run would otherwise make the scratch dir look non-empty from the
# very first poll -- before this run's own command has created anything --
# and every check below would report leftovers regardless of whether this
# run actually behaved correctly.
rm -rf "$tmp"
mkdir -p "$tmp"
TMPDIR="$tmp" "$@" &
pid=$!

i=0
while [ "$i" -lt 2000 ] && [ -z "$(ls -A "$tmp" 2>/dev/null)" ] && kill -0 "$pid" 2>/dev/null; do
    sleep 0.001
    i=$((i + 1))
done

if kill -0 "$pid" 2>/dev/null; then
    echo "CAUGHT-MID-FLIGHT"
    # The driver only calls llvm::sys::RemoveFileOnSignal (registering the
    # temp file for crash-safe cleanup) once the file already exists on
    # disk -- createTemporaryFile has to create it before it can return the
    # actual randomized path to register. That leaves an unavoidable, if
    # normally sub-millisecond, gap between "file appears" (what the poll
    # above just detected) and "cleanup is registered". Under heavy CPU
    # contention (e.g. a CI job running many other compiles concurrently)
    # the scheduler can stretch that gap enough for a signal landing right
    # after detection to beat the registration, producing a real but
    # spurious SCRATCH-DIR-HAS-LEFTOVERS unrelated to the fix being tested.
    # A short settle delay here is far more than that in-memory list-append
    # needs to complete even on a loaded runner, without weakening the
    # test: it still kills the driver well before a real compile finishes.
    sleep 0.02
    kill -TERM -- -"$pid" 2>/dev/null
else
    echo "FINISHED-BEFORE-SIGNAL"
fi
wait "$pid" 2>/dev/null

if [ -z "$(ls -A "$tmp" 2>/dev/null)" ]; then
    echo "SCRATCH-DIR-CLEAN"
else
    echo "SCRATCH-DIR-HAS-LEFTOVERS"
fi
