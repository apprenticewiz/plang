(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1.50
CHECK-NEXT:k
CHECK-NEXT:7 9
*)

program p;
type rec = record a, b: integer end;
var r: real; c: char; q: rec;
    x: type of r; y: type of c; z: type of q;
begin
  x := 1.5;  writeln(x:0:2);
  y := 'k';  writeln(y);
  z.a := 7; z.b := 9; writeln(z.a, ' ', z.b)
end.
