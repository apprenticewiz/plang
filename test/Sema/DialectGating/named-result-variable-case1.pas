(*
RUN: %plang_ep -dump-ast %s
*)

program p(output);
function F = r: integer;
begin
  r := 5
end;
begin
  writeln(F)
end.
