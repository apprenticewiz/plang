(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 3
*)

program p;
const c = (-17) mod 5;
var a, b: integer;
begin a := -17; b := 5; writeln(c, ' ', a mod b) end.
