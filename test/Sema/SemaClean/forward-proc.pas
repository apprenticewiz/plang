(*
RUN: %plang -dump-ast %s
*)

program p;
procedure foo(x : integer); forward;
procedure foo(x : integer); begin end;
begin end.
