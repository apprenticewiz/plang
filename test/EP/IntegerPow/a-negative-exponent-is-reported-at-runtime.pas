(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: negative exponent
*)

program p(output); var i, j: integer;
begin j := -3; i := 2 pow j; writeln(i) end.
