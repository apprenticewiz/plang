(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p(output);
var a: 1..10; b: 1..100;
begin b := 5; a := b; writeln(a:1) end.
