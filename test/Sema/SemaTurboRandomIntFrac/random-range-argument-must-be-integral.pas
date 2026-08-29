(*
Random(Range), the one-argument shape, answers an integer-kind value in
[0, Range) -- Range itself has to be integral (Sema::checkCallExpr's own
Random arm), the same shape check every other single-typed-argument
builtin (Card/err_set_argument, Abs+Sqr/High+Low/err_numeric_argument, ...)
already gets.  A real Range has no integer count of values to pick from.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'random' requires a numeric argument, got 'real'
*)

program p;
var n: Integer;
begin
  n := Random(3.14);
end.
