(*
Turbo Tier 4, Cluster C item 5: KeyPressed/ReadKey (runtime/plang_crt.cpp)
against a REAL pseudo-terminal (test/tools/run-under-pty.py -- see its own
comment for why piped stdin cannot exercise this: it is never a tty, so
ensureRawMode's tcgetattr fails and it silently leaves the terminal alone).
Three things checked in one run, sent as one byte stream to keep this to a
single PTY session:
  1. KeyPressed is FALSE before anything has been typed.
  2. A plain key ('A', byte 65) is read back whole and unmodified.
  3. An arrow key (the real byte sequence ESC [ A a terminal sends for Up)
     is resolved through TP's own two-call extended-key protocol: THIS
     ReadKey call returns #0, and the VERY NEXT call -- not any other
     function -- returns the Borland scan code (72 for Up), read here by
     calling ReadKey again.

RUN: %plang -std=turbo %s -o %t
RUN: %run_under_pty "b'A\x1b[A'" %t | FileCheck %s
REQUIRES: python3-pty
*)
program ReadKeyPty;
uses Crt;
var
  C: Char;
begin
  Writeln('kp0=', KeyPressed);
  C := ReadKey;
  Writeln('c1=', Ord(C));
  C := ReadKey;
  if C = #0 then
  begin
    C := ReadKey;
    Writeln('c2ext=', Ord(C));
  end
  else
    Writeln('c2=', Ord(C));
end.
(*
CHECK: kp0=FALSE
CHECK-NEXT: c1=65
CHECK-NEXT: c2ext=72
*)
