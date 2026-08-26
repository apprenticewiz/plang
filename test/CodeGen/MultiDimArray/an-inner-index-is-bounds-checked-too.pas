(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: array index 5 out of bounds 1..3
*)

program p(output);
var a: array[1..2, 1..3] of integer; i: integer;
begin i := 5; a[1, i] := 1 end.
