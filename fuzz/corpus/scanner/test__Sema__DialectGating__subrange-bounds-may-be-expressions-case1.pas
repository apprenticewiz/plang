(*
RUN: %plang_ep -dump-ast %s
*)

program p(output);
const n = 5;
type t = 1..n+1;
var x: t;
begin x := 1 end.
