(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 999 13 42
*)

program p(output);
type row = array[1..3] of integer;
     mat = array[1..2] of row;
var m: mat; after: integer; i, j: integer;
procedure poke(var a: array[lo..hi: integer] of row);
begin a[1][2] := 999 end;
begin
  after := 42;
  for i := 1 to 2 do for j := 1 to 3 do m[i][j] := i * 10 + j;
  poke(m);
  writeln(m[1][1], ' ', m[1][2], ' ', m[1][3], ' ', after)
end.
