(*
Issue #661's `{$I-}` twin of typed-file-read-at-eof-traps-runtime-error-100-
under-default-i-plus.pas, right next to this file: under `{$I-}` the same
past-EOF Read must not abort at all, just leave IOResult reading 100 (and
the destination variable, pre-zeroed by plang_read_binary_turbo before the
attempt, at 0) so the program can keep running. Confirmed against the
local `fpc -Mtp` 3.2.2 install.

Issue #738 update: the second `writeln` below starts with InOutRes already
pending (100, just set by the past-EOF read right above it) -- confirmed
against `fpc -Mtp`: unlike the simpler "one leading literal" case every
other test in this item's fix has, here TWO write attempts precede the
IOResult argument ('x=' AND the value of x itself), and BOTH are
suppressed, not just the literal -- only the value IOResult itself returns
prints (there is no ' io=' literal after it, since IOResult is the LAST
argument here).  The first writeln (InOutRes still 0, the first read
succeeded) is unaffected.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:x=42 io=0
CHECK-NEXT:100
*)

var f: file of integer; x, y: integer;
begin
  y := 42;
  assign(f, 'typed-file-read-at-eof-sets-ioresult-100.dat');
  rewrite(f);
  write(f, y);
  reset(f);
  {$I-}
  read(f, x);
  writeln('x=', x, ' io=', IOResult);
  {$I-}
  read(f, x);           { past EOF }
  writeln('x=', x, ' io=', IOResult);
end.
