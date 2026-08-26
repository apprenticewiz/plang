(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9 1 3
CHECK-NEXT:12 2 3 4
*)

program p;
type shape = record
  area: integer;
  case kind: 1..2 of
    1: (side: integer);
    2: (w, h: integer)
end;
var s: shape;
begin
  s := shape[area: 9; case kind: 1 of [side: 3]];
  writeln(s.area, ' ', s.kind, ' ', s.side);
  s := shape[area: 12; case kind: 2 of [w: 3; h: 4]];
  writeln(s.area, ' ', s.kind, ' ', s.w, ' ', s.h)
end.
