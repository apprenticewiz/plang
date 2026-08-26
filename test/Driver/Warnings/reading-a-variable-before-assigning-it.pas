(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'i' is read here before
*)

program p(output);
var i, j: integer;
begin j := 0; if j = 0 then i := 1; writeln(i) end.
