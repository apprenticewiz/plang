(*
6.7.2.2 makes it an error, reported when the program runs.  With a
constant divisor the trap is certain wherever the statement is reached,
which is worth saying earlier -- but not worth rejecting the program
over, since a statement nothing reaches commits no error.
*)

(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: by a constant zero
*)

program p(output);
var i: integer;
begin i := 0; if i <> 0 then writeln(1 mod 0) end.
