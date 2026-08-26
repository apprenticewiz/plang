(*
RUN: %plang %s -o %t
RUN: %run %t
*)

program p;
var f: file of integer;
procedure w(var g: file of integer);
begin rewrite(g); write(g, 7) end;
begin w(f) end.
