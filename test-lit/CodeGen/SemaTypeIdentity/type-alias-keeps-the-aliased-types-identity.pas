(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4
*)

program p;
type a = record x: integer end; b = a;
var s: a; t: b;
procedure w(var q: a);
begin q.x := 4 end;
begin w(t); s := t; writeln(s.x) end.
