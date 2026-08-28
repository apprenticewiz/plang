(*
mod's own divide-by-zero, distinct from emitDivZeroCheck's (which div
alone reaches -- see the sibling division-by-zero test).
emitModDivisorCheck's guard changes shape under Turbo: ISO's
divisor-must-be-POSITIVE rule (<= 0) is gone entirely (a negative divisor
is legal Turbo, see the mod-with-a-negative-divisor sibling test), but a
divisor of exactly zero is still checked and still aborts -- confirmed
against `fpc -Mtp`: `a mod 0` reports "Runtime error 200", the SAME number
`a div 0` does, not a silent hardware trap.  Caught for real while writing
this file's own sibling: an earlier draft of the Turbo mod guard returned
unconditionally without checking anything at all, which made THIS specific
case (divisor exactly zero) an unguarded srem-by-zero instead of a clean
runtime error -- worth its own regression test precisely because it is easy
to get wrong by conflating "not positive" (which Turbo drops) with "not
zero" (which Turbo keeps).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 200 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 200 at $
CHECK-NOT: plang runtime:
*)

program modbyzero;
var
  a, b, c: Integer;
begin
  a := 7;
  b := 0;
  c := a mod b;
  writeln('unreachable: ', c);
end.
