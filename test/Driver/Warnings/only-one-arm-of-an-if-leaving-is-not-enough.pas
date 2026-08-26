(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: cannot be reached
*)

program p(output);
label 9;
var i: integer;
begin i := 1; if i = 1 then goto 9; writeln('live'); 9: writeln('end') end.
