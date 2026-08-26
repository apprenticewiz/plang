(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 1 1 
CHECK-NEXT:7
*)

program p;
type row = array[1..3] of integer;
     pt  = record x, y: integer end;
const origin = pt[x: 3; y: 4];
procedure show;
const v = row[1: 7; otherwise 1];
var i: integer;
begin
  for i := 1 to 3 do write(v[i], ' ');
  writeln
end;
begin
  show;
  writeln(origin.x + origin.y)
end.
