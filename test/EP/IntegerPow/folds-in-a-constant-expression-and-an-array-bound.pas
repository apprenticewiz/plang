(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8 5
*)

program p(output); const k = 2 pow 3;
var a: array[1..k] of integer;
begin a[8] := 5; writeln(k, ' ', a[8]) end.
