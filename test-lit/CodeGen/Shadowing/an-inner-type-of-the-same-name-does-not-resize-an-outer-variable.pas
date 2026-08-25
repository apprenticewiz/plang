(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 6 9 12 15 18 21 24 27 30 
*)

program p(output);
type t = array[1..10] of integer;
var g: ^t; i: integer;
procedure inner;
type t = array[1..2] of integer;
var q: ^t;
begin new(q); q^[1] := 0; new(g) end;
begin
  inner;
  for i := 1 to 10 do g^[i] := i * 3;
  for i := 1 to 10 do write(g^[i]:1, ' ');
  writeln
end.
