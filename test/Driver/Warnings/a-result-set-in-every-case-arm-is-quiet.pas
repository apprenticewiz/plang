(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: every path
*)

program p(output);
function f(x: 1..2): integer;
begin case x of 1: f := 10; 2: f := 20 end end;
begin writeln(f(1)) end.
