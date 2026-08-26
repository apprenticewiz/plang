(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p;
type Vec(n: integer) = array[1..n] of integer;
var v: Vec(7);
begin
  with v do writeln(n)
end.
