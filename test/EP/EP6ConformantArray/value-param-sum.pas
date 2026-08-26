(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:150
*)

program p;
function sumArr(A: array [lo..hi : integer] of integer) : integer;
var i, s: integer;
begin
  s := 0;
  for i := lo to hi do s := s + A[i];
  sumArr := s
end;
var arr: array [1..5] of integer;
begin
  arr[1] := 10; arr[2] := 20; arr[3] := 30;
  arr[4] := 40; arr[5] := 50;
  writeln(sumArr(arr))
end.
