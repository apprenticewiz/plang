(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:20
*)

program p;
type Point = record x, y: integer end;
var pt: Point;
begin
  pt.x := 10; pt.y := 20;
  writeln(pt.x); writeln(pt.y)
end.
