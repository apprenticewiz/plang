(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 23 23
*)

program p(output);
var a: array[1..2, 1..3] of integer; i, j: integer;
begin
  for i := 1 to 2 do for j := 1 to 3 do a[i, j] := i * 10 + j;
  writeln(a[1, 1], ' ', a[2, 3], ' ', a[2][3])
end.
