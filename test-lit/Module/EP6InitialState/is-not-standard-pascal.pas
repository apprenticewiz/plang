(*
RUN: not %plang %s -o %t
*)

program p(output);
type counter = integer value 7;
var n: counter;
begin writeln(n) end.
