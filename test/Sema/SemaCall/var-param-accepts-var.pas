(*
RUN: %plang -dump-ast %s
*)

program p;
var a : integer;
procedure f(var x : integer); begin end;
begin f(a) end.
