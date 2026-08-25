(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:22
CHECK-NEXT:22
*)

program p(output);
type rec = record a: integer; b: integer end;
var ptr: ^rec;
procedure q;
type rec = record x: char; y: char; z: integer end;
var l: rec;
begin l.x := 'a'; writeln(ptr^.b) end;
begin new(ptr); ptr^.a := 11; ptr^.b := 22; writeln(ptr^.b); q end.
