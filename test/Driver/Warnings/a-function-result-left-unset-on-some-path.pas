(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not assign to its result on every path
*)

program p(output);
function f(x: integer): integer;
begin if x > 0 then f := 1 end;
begin writeln(f(-1)) end.
