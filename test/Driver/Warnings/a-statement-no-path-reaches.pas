(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
CHECK:live
ERR: cannot be reached
*)

program p(output);
label 9;
begin goto 9; writeln('dead'); 9: writeln('live') end.
