(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p;
var s: 1..10; i: integer;
begin i := 7; s := i; writeln(s) end.
