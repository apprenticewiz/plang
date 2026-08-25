(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
*)

program p;
var i: integer;
begin i := succ(5); writeln(i) end.
