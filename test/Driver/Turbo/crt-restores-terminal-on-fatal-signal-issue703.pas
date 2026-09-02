(*
Issue #703: Crt's own raw-mode restore (runtime/plang_crt.cpp's
restoreTerminalAtExit) used to run ONLY from an atexit handler, which a
fatal signal (SIGINT/SIGTERM/SIGHUP -- Ctrl-C, a plain `kill`, the
controlling terminal going away) never reaches: the default disposition for
all three is to terminate the process directly, with no atexit handler list
consulted at all. A program killed while blocked in ReadKey (raw mode:
ICANON/ECHO off) left the real terminal in that state afterward.  The fix
(installFatalSignalHandlers, runtime/plang_crt.cpp) installs a handler for
each that restores the terminal, then re-arms SIG_DFL and re-raises, so the
process still dies exactly as an uncaught signal normally would.

Needs a REAL pty (see test/tools/kill-under-pty-and-check-terminal.py's own
comment for why) -- this program blocks in ReadKey, giving the harness a
window to confirm raw mode is actually active before sending the signal.

RUN: %plang -std=turbo %s -o %t
RUN: %kill_under_pty SIGTERM %t | FileCheck %s
RUN: %kill_under_pty SIGINT  %t | FileCheck %s
REQUIRES: python3-pty
*)
program SignalRestoresTerminal;
uses Crt;
var
  C: Char;
begin
  C := ReadKey; { blocks -- never actually returns, the pty harness kills us }
end.
(*
CHECK: RAW-MODE-CONFIRMED
CHECK-NEXT: TERMINAL-STATE-RESTORED
*)
