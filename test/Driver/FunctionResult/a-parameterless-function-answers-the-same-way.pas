(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 3
*)

program p;
type pt = record x, y: integer end;
function origin: pt;
var t: pt;
begin t.x := 3; t.y := 4; origin := t end;
begin writeln(origin.x + origin.y, ' ', origin().x) end.
