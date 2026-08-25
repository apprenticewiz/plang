(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:64
*)

program p;
const k = 4;
var a: array[1..k*2] of integer; i: integer;
begin for i := 1 to 8 do a[i] := i * i; writeln(a[8]) end.
