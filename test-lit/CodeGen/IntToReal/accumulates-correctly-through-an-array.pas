(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6.00
*)

program p;
var a: array[1..3] of real; i: integer; s: real;
begin for i := 1 to 3 do a[i] := i;
 s := 0; for i := 1 to 3 do s := s + a[i]; writeln(s:0:2) end.
