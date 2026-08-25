(*
RUN: %plang %s -o %t
RUN: not %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: case value 99 matches no label
*)

program p;
var i: integer;
begin i := 99; case i of 1: writeln('one'); 2: writeln('two') end end.
