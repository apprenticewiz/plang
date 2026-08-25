(*
RUN: %plang -Wno-no-such-warning %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: unknown warning
*)

program p(output);
begin writeln(1) end.
