(*
RUN: %plang -g %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
CHECK:hi
ERR-ABSENT-NOT: Unknown command line argument
ERR-ABSENT-NOT: does not emit debug information
*)

program p(output);
begin writeln('hi') end.
