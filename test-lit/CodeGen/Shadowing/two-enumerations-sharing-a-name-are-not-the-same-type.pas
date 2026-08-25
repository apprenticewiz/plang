(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: two different types that share a name
*)

program p(output);
type e = (red, green, blue);
var g: e;
procedure inner;
type e = (mon, tue, wed, thu);
var l: e;
begin l := thu; g := l end;
begin inner; writeln(ord(g):1) end.
