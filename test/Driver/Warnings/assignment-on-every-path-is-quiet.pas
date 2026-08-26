(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: before it has been given
*)

program p(output);
var i, j: integer;
begin j := 0; if j = 0 then i := 1 else i := 2; writeln(i) end.
