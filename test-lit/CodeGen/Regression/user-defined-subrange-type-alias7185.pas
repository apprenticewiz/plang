(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p;
type Count = 1..10;
var n: Count;
begin n := 7; writeln(n) end.
