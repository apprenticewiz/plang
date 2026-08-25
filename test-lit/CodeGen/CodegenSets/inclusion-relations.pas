(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false true true true
*)

program p;
var a, b: set of 1..10;
begin
  a := [1,2,3]; b := [1,2,3,4,5];
  writeln(a <= b, ' ', b <= a, ' ', b >= a, ' ', a = a, ' ', a <> b)
end.
