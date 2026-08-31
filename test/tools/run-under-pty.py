#!/usr/bin/env python3
"""Runs a program under a real pseudo-terminal, sends it fixed input bytes,
and prints whatever the program wrote back to stdout.

Turbo Tier 4, Cluster C item 5: Crt's own KeyPressed/ReadKey need a REAL tty
to test the raw-mode behavior runtime/plang_crt.cpp's own ensureRawMode sets
up (tcgetattr/tcsetattr) -- piped stdin, the ordinary way this project's own
lit RUN lines feed a program input, is never a tty, so tcgetattr on it fails
and ensureRawMode silently leaves the terminal alone; a test built on a pipe
could not tell that apart from ensureRawMode having a real bug.  This
project has no other PTY-testing infrastructure to build on, so this is a
small, generic, single-purpose script (this directory's own existing
run-with-stdin-held-open.sh/kill-during-compile.sh precedent for "lit's own
internal shell cannot do this, so it is a real external script instead" --
see those two's own comments) rather than anything Pascal-source-specific.

Usage: run-under-pty.py <input-bytes-as-a-python-bytes-literal> <program> [args...]

The input is a Python bytes literal (e.g. "b'A'", "b'\\x1b[A'") passed as a
single argv string and eval'd here -- this script's only caller is this
project's own lit suite (test/lit.cfg.py's %run_under_pty substitution),
not untrusted input, so this is simpler than inventing yet another ad hoc
byte-string encoding for the same purpose.
"""
import os
import pty
import select
import sys
import time


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: run-under-pty.py <bytes-literal> <program> [args...]", file=sys.stderr)
        return 2
    input_bytes = eval(sys.argv[1], {"__builtins__": {}}, {})
    if not isinstance(input_bytes, (bytes, bytearray)):
        print("run-under-pty.py: argument 1 must be a bytes literal", file=sys.stderr)
        return 2
    prog = sys.argv[2:]

    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(prog[0], prog)
        os._exit(127)

    # Give the child time to reach its own first ReadKey/KeyPressed call and
    # install raw mode before anything is sent -- generous on purpose, this
    # only affects how long the test takes to run, not what it checks.
    time.sleep(0.3)
    os.write(fd, bytes(input_bytes))

    out = b""
    deadline = time.time() + 5.0
    idle_since = None
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.3)
        if not ready:
            # No more output for a while after having received some: the
            # program is done writing (it will exit once it has read
            # everything it asked for). Do not wait out the full deadline
            # every time -- most runs finish in well under a second.
            if out and (idle_since is None or time.time() - idle_since > 0.5):
                break
            if out and idle_since is None:
                idle_since = time.time()
            continue
        idle_since = None
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out += chunk

    try:
        os.waitpid(pid, 0)
    except OSError:
        pass

    sys.stdout.buffer.write(out)
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
