(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: more than one arm
*)

program p(output);
var i: integer;
begin case i of 1: writeln(1); 1: writeln(2) end end.
