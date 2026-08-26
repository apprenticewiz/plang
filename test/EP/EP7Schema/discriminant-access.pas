(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p;
type Vector(n: integer) = array[1..n] of integer;
var v: Vector(5);
begin
  writeln(v.n)
end.
