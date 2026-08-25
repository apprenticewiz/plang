(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
*)

program p;
type Vec(n: integer) = array[1..n] of integer;
type VecPtr = ^Vec(4);
var ptr: VecPtr;
begin
  new(ptr);
  ptr^[1] := 99;
  writeln(ptr^[1])
end.
