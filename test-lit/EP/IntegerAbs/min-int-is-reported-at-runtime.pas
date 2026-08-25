(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: no representable positive result
*)

program p(output); var n: integer;
begin n := -9223372036854775807; n := n - 1; writeln(abs(n)) end.
