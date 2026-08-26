(*
RUN: %plang -std=iso10206 %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: read here before
*)

program p(output);
type t = integer value 5;
var x: t;
begin writeln(x) end.
