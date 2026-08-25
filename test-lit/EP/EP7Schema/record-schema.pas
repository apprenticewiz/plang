(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:42
CHECK-NEXT:3
*)

program p;
type Pair(n: integer) = record
  x: array[1..n] of integer;
  y: integer
end;
var p2: Pair(3);
begin
  p2.x[1] := 7;
  p2.y := 42;
  writeln(p2.x[1]);
  writeln(p2.y);
  writeln(p2.n)
end.
