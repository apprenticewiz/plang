(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
CHECK-NEXT:3
CHECK-NEXT:5
*)

program p;
type Mat(m: integer; n: integer) = array[1..m] of array[1..n] of real;
var A: Mat(2, 3);
begin
  A[1][2] := 5.0;
  writeln(A.m);
  writeln(A.n);
  writeln(A[1][2]:1:0)
end.
