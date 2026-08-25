(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:40
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var v: vec(4); i: integer;
begin for i := 1 to 4 do v[i] := i * 10; writeln(v[4]) end.
