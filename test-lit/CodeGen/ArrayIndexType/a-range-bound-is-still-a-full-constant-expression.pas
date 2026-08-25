(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:64
*)

program p(output);
const k = 4;
var a: array[1..k * 2] of integer;
begin a[8] := 64; writeln(a[8]) end.
