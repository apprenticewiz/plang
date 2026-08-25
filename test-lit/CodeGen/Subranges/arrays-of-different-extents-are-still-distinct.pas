(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot assign
*)

program p;
var a: array[1..10] of integer; b: array[1..5] of integer;
begin a := b end.
