(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not defined for sets
*)

program p;
var s: set of 1..10;
begin s := [1]; if s < s then writeln('x') end.
