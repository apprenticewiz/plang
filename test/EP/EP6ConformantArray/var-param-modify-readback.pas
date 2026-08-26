(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
procedure setFirst(var A: array [lo..hi : integer] of integer; v: integer);
begin A[lo] := v end;
var arr: array [2..5] of integer;
begin
  arr[2] := 0;
  setFirst(arr, 42);
  writeln(arr[2])
end.
