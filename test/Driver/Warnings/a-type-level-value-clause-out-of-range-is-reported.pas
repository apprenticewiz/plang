(*
RUN: %plang -std=iso10206 %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 99 is outside the range 1..5
*)

program p(output);
type t1 = 1..5 value 99;
var x: t1;
begin writeln(x) end.
