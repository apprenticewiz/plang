(*
FillChar(var X; ...) and Move(const Source; var Dest; ...) each need a real
address for their "untyped" argument(s) -- checkCallStmt's own arms reuse
err_var_param_needs_lvalue (the same diagnostic an ordinary 'var' parameter
already gives a non-variable actual) for FillChar's X and for both of
Move's Source and Dest.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: argument 1 of 'fillchar' is a 'var' parameter and requires a variable
CHECK: argument 1 of 'move' is a 'var' parameter and requires a variable
CHECK: argument 2 of 'move' is a 'var' parameter and requires a variable
*)

program p;
var
  buf: array[1 .. 4] of Char;
  n: Integer;
begin
  FillChar(n + 1, 1, ' ');
  Move(n + 1, buf[1], 1);
  Move(buf[1], n + 1, 1);
end.
