(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
CHECK-NEXT:7
*)

program p;
type Pair = record x, y: integer end;
var pt: Pair;
begin
  pt.x := 5; pt.y := 7;
  with pt do begin x := x + 1; writeln(x); writeln(y) end
end.
