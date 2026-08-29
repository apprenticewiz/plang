(*
TP `Int(x)`/`Frac(x)` (Builtins.def, -std=turbo only): x's integer part,
toward zero -- the same direction as Trunc, but a REAL result rather than an
ordinal one -- and its fractional part, x - Int(x), keeping x's own sign
(Frac(-3.7) is -0.7, not 0.3: there is no floor here, only the same
toward-zero Int).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.0
CHECK-NEXT:0.7000
CHECK-NEXT:-3.0
CHECK-NEXT:-0.7000
*)

program p;
var r: Real;
begin
  r := 3.7;
  writeln(Int(r):0:1);
  writeln(Frac(r):0:4);

  r := -3.7;
  writeln(Int(r):0:1);
  writeln(Frac(r):0:4);
end.
