(*
RUN: %plang -dump-ast %s
*)

program p(output);
var date: integer;
begin date := 3; writeln(date) end.
