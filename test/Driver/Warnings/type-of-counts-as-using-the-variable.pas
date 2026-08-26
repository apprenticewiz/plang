(*
RUN: %plang -std=iso10206 %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: never used
*)

program p(output);
var x: integer;
var y: type of x;
begin y := 5; writeln(y) end.
