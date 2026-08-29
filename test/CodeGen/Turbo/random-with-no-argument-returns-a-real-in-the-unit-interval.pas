(*
TP `Random` (Builtins.def, -std=turbo only), the zero-argument shape: a REAL
in [0, 1).  Exercises both call spellings -- bare (`Random`, routed through
CGExprCore::emitExpr's IdentExpr case next to eof/eoln's identical bare-call
handling) and parenthesized-with-no-arguments (`Random()`, a genuinely
distinct CallExpr with Args.empty() -- see Sema::checkCallExpr's own Random
arm) -- across many iterations: a real, if statistical rather than
cryptographic, sanity check that every result actually lands in the promised
range, not just that the call compiles.

This is plang's OWN generator (runtime/plang_math.cpp's
plang_tp_random_real): no claim is made that its sequence matches real
Borland Turbo Pascal 7's own 32-bit LCG or Free Pascal's Mersenne Twister --
the two do not agree with each other either, and matching either bit-for-bit
is not a goal here.  See randseed-set-to-the-same-value-makes-random-
deterministic.pas for the determinism anchor test, and
random-with-a-range-argument-returns-an-integer-in-that-range.pas for the
one-argument shape.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bare-in-range=TRUE
CHECK-NEXT:paren-in-range=TRUE
*)

program p;
var
  i: Integer;
  r: Real;
  bareOk, parenOk: Boolean;
begin
  bareOk := true;
  for i := 1 to 5000 do begin
    r := Random;
    if (r < 0.0) or (r >= 1.0) then bareOk := false;
  end;
  writeln('bare-in-range=', bareOk);

  parenOk := true;
  for i := 1 to 5000 do begin
    r := Random();
    if (r < 0.0) or (r >= 1.0) then parenOk := false;
  end;
  writeln('paren-in-range=', parenOk);
end.
