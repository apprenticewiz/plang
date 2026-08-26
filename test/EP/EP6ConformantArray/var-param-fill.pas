(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:7
CHECK-NEXT:7
CHECK-NEXT:7
*)

program p;
procedure fill(var A: array [lo..hi : integer] of integer; v: integer);
var i: integer;
begin
  for i := lo to hi do A[i] := v
end;
var arr: array [1..4] of integer;
var i: integer;
begin
  fill(arr, 7);
  for i := 1 to 4 do writeln(arr[i])
end.
