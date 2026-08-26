(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: two different types that share a name
*)

program p(output);
type r = record a: integer end;
var g: r;
procedure inner;
type r = record a, b, c: integer end;
var l: r;
begin l.a := 1; l.b := 2; l.c := 3; g := l end;
begin inner; writeln(g.a:1) end.
