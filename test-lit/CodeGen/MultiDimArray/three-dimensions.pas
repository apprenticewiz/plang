(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
var t: array[1..2, 1..2, 1..2] of integer;
begin t[1, 2, 1] := 42; writeln(t[1, 2, 1]) end.
