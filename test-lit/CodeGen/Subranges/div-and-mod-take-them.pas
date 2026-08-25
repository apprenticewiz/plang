(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 35 -7
*)

program p(output);
var a, b: 0..100; c: -100..100;
begin
  a := 78; b := 43; c := -50;
  writeln(a div b:1, ' ', a mod b:1, ' ', c div 7:1)
end.
