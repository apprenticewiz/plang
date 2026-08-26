(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9 12 15 18 
*)

program p;
var a: array[1..10] of integer;
    z: packed array[1..4] of integer;
    i: integer;
begin
  for i := 1 to 10 do a[i] := i * 3;
  pack(a, 3, z);
  for i := 1 to 4 do write(z[i], ' ');
  writeln
end.
