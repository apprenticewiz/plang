(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9
*)

program p;
type a = record x: integer end;
var s, t: a;
begin t.x := 9; s := t; writeln(s.x) end.
