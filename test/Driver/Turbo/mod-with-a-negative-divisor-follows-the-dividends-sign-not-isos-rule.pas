(*
ISO §6.7.2.2 defines mod only for a positive divisor (RangeCheckGuards::
emitModDivisorCheck's ISO/EP path, unchanged by this file) and normalizes
the result to 0 <= mod < divisor (CGBinaryOps.cpp's Mod case).  Real Turbo
Pascal has neither rule: a negative divisor is legal, and the result takes
the DIVIDEND's sign -- exactly what LLVM's own srem already computes, so
Turbo's mod is srem with no ISO adjustment layered on top.  Every sign
combination here is confirmed against `fpc -Mtp` directly (not just
reasoned from the rule): 7 mod (-3) = 1, (-7) mod (-3) = -1,
(-7) mod 3 = -1, 7 mod 3 = 1 -- the last two also pin down that flipping
the DIVIDEND's sign (not the divisor's) is what changes the answer here,
which ISO's normalized rule would not (ISO's 0 <= mod < divisor never
depends on the dividend's sign at all).  The whole point is that none of
these abort: a negative divisor reaching emitModDivisorCheck's ISO path
would be refused outright as a dynamic-violation.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:-1
CHECK-NEXT:-1
CHECK-NEXT:1
*)

program modnegativedivisor;
begin
  writeln(7 mod (-3));
  writeln((-7) mod (-3));
  writeln((-7) mod 3);
  writeln(7 mod 3);
end.
