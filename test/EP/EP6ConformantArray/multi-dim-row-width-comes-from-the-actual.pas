(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 8
*)

program p;
type small = array[1..2, 1..2] of integer;
     big   = array[1..2, 1..4] of integer;
var s: small;
    b: big;
    i, j: integer;
function corner(var x: array[l1..h1: integer;
                             l2..h2: integer] of integer): integer;
begin corner := x[h1, h2] end;
begin
  for i := 1 to 2 do for j := 1 to 2 do s[i, j] := i * j;
  for i := 1 to 2 do for j := 1 to 4 do b[i, j] := i * j;
  writeln(corner(s), ' ', corner(b))
end.
