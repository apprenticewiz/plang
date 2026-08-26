(*
RUN: %plang_ep -dump-ast %s
*)

program p(output);
var i: integer;
begin i := 1; case i of 1: ; else ; end end.
