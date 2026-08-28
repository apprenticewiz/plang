(*
Turbo's '@' takes the address of a variable (Sema::checkUnary's At case),
so its operand has to be an lvalue the same way a 'var' parameter's
argument does -- '@(1+1)' has no storage to point at.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var x: Integer;
begin
  x := @(1 + 1)
end.

(*
CHECK: '@' requires a variable
*)
