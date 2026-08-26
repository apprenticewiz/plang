(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:23 5 103
CHECK-NEXT:4 18 7
*)

program p(output);
type mat(r, c: integer) = record
  m: array[1..r, 1..c] of integer;
  edge: array[1..2*r+1] of integer;
  inner: record w: array[1..c] of integer end
end;
var a: mat(2, 3); b: mat(4, 1); i, j: integer;
begin
  for i := 1 to 2 do for j := 1 to 3 do a.m[i, j] := i * 10 + j;
  for i := 1 to 5 do a.edge[i] := i;
  for i := 1 to 3 do a.inner.w[i] := 100 + i;
  for i := 1 to 4 do b.m[i, 1] := i;
  for i := 1 to 9 do b.edge[i] := i * 2;
  b.inner.w[1] := 7;
  writeln(a.m[2, 3]:0, ' ', a.edge[5]:0, ' ', a.inner.w[3]:0);
  writeln(b.m[4, 1]:0, ' ', b.edge[9]:0, ' ', b.inner.w[1]:0)
end.
