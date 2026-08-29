(*
Int/Frac share their argument check with Trunc/Round (Sema::checkCallExpr's
own combined arm): numeric, non-complex.  A ShortString has no integer or
fractional part to extract.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'int' requires a numeric argument, got 'string[5]'
CHECK: 'frac' requires a numeric argument, got 'string[5]'
*)

program p;
var
  r: Real;
  s: string[5];
begin
  s := 'hello';
  r := Int(s);
  r := Frac(s);
end.
