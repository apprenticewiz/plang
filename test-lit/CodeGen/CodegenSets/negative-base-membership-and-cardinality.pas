(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 true true false true
*)

program p;
var s: set of -5..10;
begin
  s := [-5, -1, 0, 3];
  writeln(card(s), ' ', -5 in s, ' ', -1 in s, ' ', -2 in s, ' ', 3 in s)
end.
