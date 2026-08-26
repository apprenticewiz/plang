(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:outer 4
CHECK-NEXT:inner K
*)

program p;
type r = record x: integer end;
var g: r;
procedure q; type r = record y: char end; var v: r;
begin v.y := 'K'; writeln('inner ', v.y) end;
begin g.x := 4; writeln('outer ', g.x); q end.
