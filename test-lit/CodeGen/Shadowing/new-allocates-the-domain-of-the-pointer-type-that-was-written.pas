(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
*)

program p(output);
type t  = array[1..10] of integer;
     pt = ^t;
var g: pt; i: integer;
procedure inner;
type t2 = array[1..2] of integer;
     pt = ^t2;
begin new(g) end;
begin
  inner;
  for i := 1 to 10 do g^[i] := i;
  writeln(g^[10])
end.
