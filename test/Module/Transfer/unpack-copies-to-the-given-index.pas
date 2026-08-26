(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 0 0 0 5 10 15 20 0 0 
*)

program p;
var b: array[1..10] of integer;
    z: packed array[1..4] of integer;
    i: integer;
begin
  for i := 1 to 4 do z[i] := i * 5;
  for i := 1 to 10 do b[i] := 0;
  unpack(z, b, 5);
  for i := 1 to 10 do write(b[i], ' ');
  writeln
end.
