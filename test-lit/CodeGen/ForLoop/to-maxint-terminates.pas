(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

program p;
var i, n: integer;
begin n := 0; for i := maxint - 2 to maxint do n := n + 1;
 writeln(n) end.
