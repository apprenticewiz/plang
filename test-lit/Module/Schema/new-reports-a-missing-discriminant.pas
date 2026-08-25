(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: needs 1 discriminant
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var q: ^vec;
begin new(q) end.
