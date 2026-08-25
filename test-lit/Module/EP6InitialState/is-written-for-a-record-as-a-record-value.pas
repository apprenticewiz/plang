(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 4
*)

program p(output);
type point = record x, y: integer end value [x: 3; y: 4];
var q: point;
begin writeln(q.x, ' ', q.y) end.
