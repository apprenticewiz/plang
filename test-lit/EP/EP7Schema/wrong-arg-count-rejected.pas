(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: discriminant
*)

program p;
type Vec(n: integer) = array[1..n] of integer;
var v: Vec(1, 2);
begin end.
