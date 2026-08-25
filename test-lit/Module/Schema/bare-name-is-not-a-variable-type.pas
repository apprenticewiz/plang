(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: discriminants
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var v: vec;
begin end.
