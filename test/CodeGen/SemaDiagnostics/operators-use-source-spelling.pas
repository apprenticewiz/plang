(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: operator '+'
ERR-ABSENT-NOT: operator 'Plus'
*)

program p;
var b: boolean; i: integer;
begin b := true; i := 1; i := b + i end.
