(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: needs 0 or 1 discriminant(s), got 2
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var q: ^vec;
begin new(q, 5); dispose(q, 1, 2) end.
