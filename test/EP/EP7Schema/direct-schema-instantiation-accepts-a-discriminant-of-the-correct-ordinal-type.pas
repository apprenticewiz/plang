(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
type Vec(n: integer) = record data: array[1..n] of integer end;
var v: Vec(5);
begin
  v.data[1] := 42;
  writeln(v.data[1])
end.
