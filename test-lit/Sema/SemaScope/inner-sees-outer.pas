(*
RUN: %plang -dump-ast %s
*)

program p;
var x : integer;
procedure inner;
begin x := 42 end;
begin end.
