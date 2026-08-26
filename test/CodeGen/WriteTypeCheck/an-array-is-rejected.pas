(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot be written
*)

program p(output); var a: array[1..3] of integer;
begin writeln(a) end.
