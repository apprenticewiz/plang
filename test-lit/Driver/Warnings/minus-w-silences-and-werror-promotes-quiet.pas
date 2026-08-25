(*
-w silences every warning, so a program that would otherwise get a
flow warning compiles and runs without one.
*)

(*
RUN: %plang %s -w -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: warning
*)

program p(output);
var i: integer;
begin writeln(i) end.
