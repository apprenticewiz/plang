(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0
*)

program p;
var i, n: integer;
begin n := 0; for i := 5 to 1 do n := n + 1; writeln(n) end.
