(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
*)

program p;
function first(A: array [lo..hi : integer] of integer) : integer;
begin first := A[lo] end;
var arr: array [0..2] of integer;
begin
  arr[0] := 99;
  writeln(first(arr))
end.
