(*
RUN: %plang -fdiagnostics-language=qps_ploc %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: unrecognized
*)

program p(output);
begin writeln('ok') end.
