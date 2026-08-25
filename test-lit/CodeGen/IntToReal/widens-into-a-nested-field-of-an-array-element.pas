(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7.0000
*)

program p;
type r = record x: real end;
var a: array[1..2] of r;
begin a[2].x := 7; writeln(a[2].x:0:4) end.
