(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
function F = r: integer;
begin
  r := 5
end;
begin
  writeln(F)
end.

(*
CHECK: a named result variable ('function name(...) = result: type') is an Extended Pascal extension and is not available under -std=iso7185
*)
