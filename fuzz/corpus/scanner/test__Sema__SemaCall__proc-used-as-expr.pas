(*
RUN: not %plang -dump-ast %s
*)

program p;
var x : integer;
procedure f; begin end;
begin x := f end.
