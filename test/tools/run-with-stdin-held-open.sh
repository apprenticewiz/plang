#!/usr/bin/env bash
# Runs a compiled program with its stdin connected to a pipe that never
# sends EOF and never has anything queued -- a real terminal's own behavior
# before the first keystroke, unlike /dev/null's immediate EOF -- so a
# program that blocks waiting to read a character it was never going to get
# is distinguishable from one that finishes on its own without needing one.
#
#   test/tools/run-with-stdin-held-open.sh ./myprog [args...]
#
# Exits with the program's own exit status if it finished within 5 seconds;
# a program that actually blocks is killed and reports 137 (128 + SIGKILL).
# The program's own stdout/stderr are inherited normally, so the caller can
# pipe or redirect this script's own output exactly as if it had run the
# program directly.
set -u
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
fifo="$d/stdin"
mkfifo "$fifo" || exit 2
# Held open read-write on a spare descriptor for as long as this script
# lives, so the fifo always has a writer -- the reader never sees EOF, and
# opening it for read does not itself block, which it would with no writer
# connected at all yet.
exec 9<>"$fifo"
"$@" <"$fifo" &
pid=$!
(sleep 5; kill -9 "$pid" 2>/dev/null) >/dev/null 2>&1 &
guard=$!
wait "$pid"
rc=$?
kill "$guard" 2>/dev/null
exec 9>&-
exit "$rc"
