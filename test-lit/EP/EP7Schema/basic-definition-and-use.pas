(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program p;
type Vector(n: integer) = array[1..n] of real;
var v: Vector(5);
begin
  v[1] := 1.0; v[2] := 2.0; v[3] := 3.0;
  writeln(v[2]:1:0)
end.
