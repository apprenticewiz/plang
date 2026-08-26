(*
RUN: %plang -dump-ast %s
*)

program p(output);
type rec = record a, b: integer end;
var r: rec; i, s: integer;
begin s := 0; with r do for i := 1 to 3 do s := s + a + b end.
