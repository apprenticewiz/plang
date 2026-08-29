(*
ISO 7185/EP's read(f, i) takes the LONGEST PREFIX of the input that parses
as a number and silently succeeds: `read(i)` on "12abc" gives i=12, with
"abc" left for whatever reads next.  Real Turbo Pascal reverses this: it
skips leading whitespace, collects the WHOLE next whitespace-delimited
token, and requires the ENTIRE token to parse as a number or it is an
error -- confirmed against `fpc -Mtp`: `read(i)` on "12abc" reports
"Runtime error 106: Invalid numeric format" and exits 106, never reaching
i=12.  plang_read_i64_turbo (runtime/plang_io.cpp) implements this via
scanTokenTurbo (collects the whole token, no number-shaped filtering) plus
a check that strtoll's own End pointer lands exactly on the token's
terminating NUL.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %checkexit 106 %run %t < %t.dir/stdin.txt 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 106 at $
*)

//--- test.pas
program p;
var i: Integer;
begin
  read(i);
  writeln('unreachable: ', i)
end.

//--- stdin.txt
12abc
