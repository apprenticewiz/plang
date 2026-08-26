(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: discriminant n differs
*)

program p(output);
type t(n: integer) = record k: integer end;
var q: ^t; v: t(4);
begin new(q, 3); q^.k := 1; v := q^; writeln(v.k:1) end.
