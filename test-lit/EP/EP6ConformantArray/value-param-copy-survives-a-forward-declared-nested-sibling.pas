(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:106
*)

program p;
var arr: array [1..3] of integer;
procedure outer(A: array [lo..hi : integer] of integer);
  procedure inner(x: integer); forward;
  procedure inner(x: integer);
  begin writeln(x) end;
var i, s: integer;
begin
  A[lo] := A[lo] + 100;
  s := 0;
  for i := lo to hi do s := s + A[i];
  inner(s)
end;
begin
  arr[1] := 1; arr[2] := 2; arr[3] := 3;
  outer(arr)
end.
