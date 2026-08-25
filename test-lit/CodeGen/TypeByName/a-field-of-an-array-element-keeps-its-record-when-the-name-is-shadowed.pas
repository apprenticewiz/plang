(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 22 33
CHECK-NEXT:11 22 33
*)

program p(output);
type rec = record a, b, c: integer end;
var arr: array[1..2] of rec;
procedure q;
type rec = record x, y, z: char end;
var l: rec;
begin l.x := 'a'; writeln(arr[1].a, ' ', arr[1].b, ' ', arr[1].c) end;
begin arr[1].a := 11; arr[1].b := 22; arr[1].c := 33;
  writeln(arr[1].a, ' ', arr[1].b, ' ', arr[1].c); q end.
