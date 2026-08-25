(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 22 33
*)

program p(output);
type r = record a, b, c: integer end;
var g: r;
procedure inner;
type r = record a: integer end;
var l: r;
begin l.a := 1; g.a := 11; g.b := 22; g.c := 33 end;
begin inner; writeln(g.a:1, ' ', g.b:1, ' ', g.c:1) end.
