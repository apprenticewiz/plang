(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false
*)

program p;
var a, b: integer;
begin a := -5; b := 3; writeln(a < b, ' ', a > b) end.
