(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-1 -25
*)

program p;
type r = -1..10; q = -50..-10;
var x: r; y: q;
begin x := -1; y := -25; writeln(x, ' ', y) end.
