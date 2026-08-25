(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1234
CHECK-NEXT:99
*)

program p(output);
type point = record x, y: integer end;
     line  = record a, b: point end
               value [a: [x: 1; y: 2]; b: [x: 3; y: 4]];
     grid  = array [1..3] of point value [otherwise [x: 9; y: 9]];
var l: line; g: grid;
begin writeln(l.a.x, l.a.y, l.b.x, l.b.y);
  writeln(g[1].x, g[3].y) end.
