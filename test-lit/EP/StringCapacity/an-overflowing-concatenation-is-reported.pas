(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: assigned to a string(4)
*)

program p(output); var s: string(4); a, b: string(4);
begin a := 'abc'; b := 'def'; s := a + b end.
