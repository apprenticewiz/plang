(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:5
CHECK-NEXT:10
CHECK-NEXT:12
*)

program p;
{ abbreviated 2D conformant syntax — reads only bound vars }
procedure showBounds(A: array [lo..hi : integer; c1..c2 : integer] of integer);
begin
  writeln(lo); writeln(hi); writeln(c1); writeln(c2)
end;
{ type-compatible actual: array of array for the 2D conformant }
type Row = array [10..12] of integer;
var mat: array [3..5] of Row;
begin
  showBounds(mat)
end.
