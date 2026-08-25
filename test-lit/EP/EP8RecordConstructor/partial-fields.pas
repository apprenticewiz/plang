(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:0
*)

program p;
type Point = record x, y: integer end;
var pt: Point;
begin
  pt := Point[x: 7];
  writeln(pt.x);
  writeln(pt.y)
end.
