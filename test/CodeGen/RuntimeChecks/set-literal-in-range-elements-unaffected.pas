(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s --strict-whitespace --match-full-lines
*)

program p;
var s: set of 1..10; x: integer;
begin
  x := 7;
  s := [1, x, 5..8];
  writeln(x in s)
end.

(*
CHECK:true
*)
