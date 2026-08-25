(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
type Vec(n: integer) = array[1..n] of integer;
function first(v: Vec(3)) : integer;
begin first := v[1] end;
var a: Vec(3);
begin
  a[1] := 42;
  writeln(first(a))
end.
