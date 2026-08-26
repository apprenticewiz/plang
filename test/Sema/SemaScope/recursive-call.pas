(*
RUN: %plang -dump-ast %s
*)

program p;
function fact(n : integer) : integer;
begin
  if n <= 0 then fact := 1
  else fact := n * fact(n - 1)
end;
begin end.
