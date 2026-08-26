(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
*)

program p;
function sumArr(A: array [lo..hi : integer] of integer) : integer;
var i, s: integer;
begin
  s := 0;
  for i := lo to hi do s := s + A[i];
  sumArr := s
end;
procedure wrapper(var B: array [lo2..hi2 : integer] of integer);
begin writeln(sumArr(B)) end;
var arr: array [1..4] of integer;
begin
  arr[1] := 1; arr[2] := 2; arr[3] := 3; arr[4] := 4;
  wrapper(arr)
end.
