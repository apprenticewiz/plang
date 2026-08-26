(*
RUN: %plang -dump-ast %s
*)

program p;
function square(x : integer) : integer;
begin square := x * x end;
begin end.
