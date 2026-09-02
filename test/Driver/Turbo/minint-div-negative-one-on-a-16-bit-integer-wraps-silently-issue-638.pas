(*
Turbo's Integer is a real, live 16-bit signed type (LangOptions::
defaultIntWidth() returns 16 under -std=turbo), so its own minint is
-32768, not ISO/EP's -9223372036854775808.  `MinInt16 div -1` has no
representable 16-bit result -- 32768 does not fit.

Issue #638: this file used to assert that plang aborts with Borland/FPC's
"Runtime error 215: Arithmetic overflow error" here, matching real Turbo
Pascal's documented behavior. Re-checked directly against `fpc -Mtp`
3.2.2, though, that is not FPC's own field practice: `MinInt div -1`
computes silently there, giving MinInt back, at every width up to Int64,
even under Turbo's `$Q+` overflow-checking directive -- FPC's own div
overflow is simply never checked.
`n`/`d` here are sign-extended to i64 before the SDiv itself
(CGBinaryOps.cpp's Div case), so the FULL, unclamped quotient (32768) is
what a program gets as long as it stays unstored -- assigning it into a
16-bit `Integer` variable is what narrows it back down, via the same
truncate-on-store wraparound every other Turbo arithmetic overflow
already goes through -- landing on MinInt16 (-32768) again, since 32768's
two's-complement 16-bit wrapped bit pattern IS -32768. See this file's own
sibling for the still-unaffected ISO 7185/Extended Pascal case (their
Integer is always Width 64, so 2^63 truly has no representation to
compute first and narrow later): division-overflow-reports.pas
(test/CodeGen/RuntimeChecks).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:32768
CHECK-NEXT:-32768
*)

program minintdivnegone;
var
  a, b, n: Integer;
begin
  n := -32768;
  a := n;
  b := -1;
  writeln(a div b);   { unstored: full-precision 32768, matching fpc -Mtp }
  a := a div b;        { stored into a 16-bit Integer: wraps back to MinInt16 }
  writeln(a)
end.
