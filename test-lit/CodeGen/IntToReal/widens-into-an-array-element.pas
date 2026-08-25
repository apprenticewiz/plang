(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5.0000
*)

program p;
var a: array[1..3] of real;
begin a[1] := 5; writeln(a[1]:0:4) end.
