(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:7
CHECK-NEXT:20
CHECK-NEXT:100
*)

program p;
type Vector(n: integer) = array[1..n] of integer;
var a: Vector(3);
var b: Vector(7);
begin
  a[1] := 10; a[2] := 20; a[3] := 30;
  b[1] := 100;
  writeln(a.n);
  writeln(b.n);
  writeln(a[2]);
  writeln(b[1])
end.
