(*
RUN: not %plang -dump-ast %s
*)

program p;
procedure f; begin end;
begin f(1) end.
