(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: leaves its control variable undefined
*)

program p(output);
var i: integer;
begin for i := 1 to 3 do writeln(i); writeln(i) end.
