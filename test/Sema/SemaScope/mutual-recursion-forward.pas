(*
RUN: %plang -dump-ast %s
*)

program p;
procedure b(n : integer); forward;
procedure a(n : integer);
begin if n > 0 then b(n - 1) end;
procedure b(n : integer);
begin if n > 0 then a(n - 1) end;
begin end.
