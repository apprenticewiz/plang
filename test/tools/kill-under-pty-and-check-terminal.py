#!/usr/bin/env python3
"""Runs a program under a real pseudo-terminal, sends it a fatal signal
once it has had time to enter raw mode, and reports whether the pty's own
terminal settings were restored afterward.

Issue #703: Crt's own raw-mode restore (runtime/plang_crt.cpp) used to run
ONLY from an atexit handler, which a fatal signal (SIGINT/SIGTERM/SIGHUP --
Ctrl-C, a plain `kill`, the controlling terminal going away) never reaches
at all -- a program killed while in raw mode (e.g. blocked in ReadKey) left
the real terminal with ICANON/ECHO off afterward. Needs a REAL tty for the
same reason test/tools/run-under-pty.py's own comment gives (piped stdin is
never a tty, so ensureRawMode's tcgetattr fails and there is no raw mode to
restore in the first place) -- this project's only other PTY-testing
infrastructure, extended here with a termios query on the master fd before
and after the signal: Linux forwards TCGETS/TCSETS through a pty's master
exactly like a real terminal's own device node, so the master side sees the
same settings a program reading/writing the slave side (stdin/stdout, once
the child execs) does, with no need to open the slave separately.

Usage: kill-under-pty-and-check-terminal.py <signal-name> <program> [args...]

Prints two lines: RAW-MODE-CONFIRMED or RAW-MODE-NEVER-ENTERED (whether the
program had actually entered raw mode -- ICANON and ECHO both off -- before
the signal was sent; RAW-MODE-NEVER-ENTERED means this run did not actually
exercise anything and any caller should treat it as inconclusive rather
than a pass, the same convention kill-during-compile.sh's own
FINISHED-BEFORE-SIGNAL uses), then TERMINAL-STATE-RESTORED or
TERMINAL-STATE-CORRUPTED (whether ICANON/ECHO are back on after the program
died). Deliberately disjoint pairs, neither a substring of the other --
kill-during-compile.sh's own comment explains why that matters: FileCheck's
default match is substring, not full-line, so an "opposite" spelled as a
plain NOT- prefix (e.g. NOT-RESTORED, which contains RESTORED) would let a
CHECK for the success string match a failure line by accident.
"""
import os
import pty
import signal
import sys
import termios
import time


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: kill-under-pty-and-check-terminal.py <signal> <program> [args...]",
              file=sys.stderr)
        return 2
    sig = getattr(signal, sys.argv[1])
    prog = sys.argv[2:]

    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(prog[0], prog)
        os._exit(127)

    # Same settle delay as run-under-pty.py's own, for the same reason: give
    # the child time to reach its own first ReadKey/KeyPressed call and
    # install raw mode before anything is checked or sent.
    time.sleep(0.3)

    before = termios.tcgetattr(fd)
    was_raw = not (before[3] & (termios.ICANON | termios.ECHO))
    print("RAW-MODE-CONFIRMED" if was_raw else "RAW-MODE-NEVER-ENTERED")

    os.kill(pid, sig)

    deadline = time.time() + 5.0
    while time.time() < deadline:
        try:
            done_pid, _ = os.waitpid(pid, os.WNOHANG)
        except OSError:
            break
        if done_pid == pid:
            break
        time.sleep(0.05)

    after = termios.tcgetattr(fd)
    restored = bool(after[3] & termios.ICANON) and bool(after[3] & termios.ECHO)
    print("TERMINAL-STATE-RESTORED" if restored else "TERMINAL-STATE-CORRUPTED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
