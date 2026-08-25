(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: parameter 'y' is never used
*)

program p(output);
procedure q(x, y: integer); begin writeln(x) end;
begin q(1, 2) end.
