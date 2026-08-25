(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: outside the range 1..10
*)

program p(output);
var i: 1..10;
begin i := 1; if i = 0 then i := 99; writeln(i) end.
