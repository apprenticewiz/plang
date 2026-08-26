(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:20
*)

program p;
type Point = record x, y: integer end;
var p: Point;
begin
  p := Point[x: 10; y: 20];
  writeln(p.x);
  writeln(p.y)
end.
