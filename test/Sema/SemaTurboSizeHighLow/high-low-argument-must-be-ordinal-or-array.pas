(*
High/Low answer either from an ordinal type/value directly or, for an
array, through its own index type -- checkCallExpr's High/Low arm refuses
anything else (err_high_low_argument), the same shape check every other
single-typed-argument builtin (Card/err_set_argument, Abs/Sqr/
err_numeric_argument, ...) already gets.  A real (non-ordinal) value has
no bounds to give.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'high' requires an ordinal or array type, got 'real'
*)

program p;
var
  x: Real;
  n: Real;
begin
  x := 3.14;
  n := High(x);
end.
