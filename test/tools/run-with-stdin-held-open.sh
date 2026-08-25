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
# A plain polling loop, not a backgrounded sleep-then-kill watchdog: two
# earlier attempts both leaked a real, separate `sleep` PROCESS that
# outlived this script (killing a wrapping subshell/job does not kill the
# `sleep` running underneath it -- `wait` for an arbitrary pid only works
# for the CALLING shell's own direct children, so a second shell can't
# even wait on it to arrange a clean handoff either). A leaked sleep gets
# reparented, keeps running, and fires its delayed kill -9 against
# whatever the kernel has since reused $pid for -- confirmed for real on
# CI: an orphaned "sleep" process got reported, and a wholly unrelated
# later test in the same job failed, exactly the shape a stray delayed
# SIGKILL against a reused pid would produce. A loop of plain, FOREGROUND
# `sleep 1` calls in this same shell has nothing left running once the
# loop exits, by construction -- no separate process to leak.
i=0
while [ "$i" -lt 5 ] && kill -0 "$pid" 2>/dev/null; do
    sleep 1
    i=$((i + 1))
done
if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null
fi
wait "$pid" 2>/dev/null
rc=$?
exec 9>&-
exit "$rc"
