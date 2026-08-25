(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:22
*)

program p(output);
type g = array[1..2, 1..2] of integer;
procedure q;
var g: array[1..2, 1..2] of integer; i, j: integer;
begin
  for i := 1 to 2 do for j := 1 to 2 do g[i,j] := i*10+j;
  writeln(g[2,2]:1)
end;
begin q end.
