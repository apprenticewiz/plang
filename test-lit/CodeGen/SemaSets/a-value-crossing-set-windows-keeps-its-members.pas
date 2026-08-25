(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  1  3
*)

program p(output);
type a = set of -5..10; b = set of 0..10;
var x: a; y: b; i: integer;
begin
  y := [1, 3];
  x := y;
  for i := -5 to 10 do if i in x then write(i:3);
  writeln
end.
