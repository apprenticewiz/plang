(*
Inc(x)/Dec(x)'s argument must be an ordinal variable or, under -std=turbo,
a PChar-like typed pointer -- checkCallStmt's Inc/Dec arm refuses anything
else (err_inc_dec_argument) before asking whether it is an lvalue.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'inc' requires an ordinal or pointer argument, got 'real'
CHECK: 'dec' requires an ordinal or pointer argument, got 'real'
*)

program p;
var
  x: Real;
begin
  x := 3.14;
  Inc(x);
  Dec(x);
end.
