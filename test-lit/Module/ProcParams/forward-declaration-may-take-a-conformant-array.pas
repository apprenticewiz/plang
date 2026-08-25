(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p;
var arr: array[1..2] of integer;
procedure show(a: array[lo..hi: integer] of integer); forward;
procedure show(a: array[lo..hi: integer] of integer);
begin writeln(a[lo]) end;
begin arr[1] := 5; show(arr) end.
