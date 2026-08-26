(*
RUN: %plang_ep -dump-ast %s
*)

program p(output);
var a: integer;
const c = 1;
begin a := c end.
