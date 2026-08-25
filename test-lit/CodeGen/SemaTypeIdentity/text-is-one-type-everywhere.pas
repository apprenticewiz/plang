(*
RUN: %plang %s -o %t
RUN: %run %t
*)

program p;
var f: text;
procedure w(var t: text);
begin writeln(t, 'ok') end;
begin rewrite(f); w(f) end.
