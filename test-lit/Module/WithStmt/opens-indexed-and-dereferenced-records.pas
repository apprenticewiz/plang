(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 22
CHECK-NEXT:33 44
*)

program p;
type pt = record x, y: integer end;
var a: array[1..3] of pt; q: ^pt;
begin
  with a[2] do begin x := 11; y := 22 end;
  writeln(a[2].x, ' ', a[2].y);
  new(q);
  with q^ do begin x := 33; y := 44 end;
  writeln(q^.x, ' ', q^.y)
end.
