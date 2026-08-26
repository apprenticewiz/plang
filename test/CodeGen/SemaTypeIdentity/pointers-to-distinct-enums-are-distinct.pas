(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR-DAG: '^c1'
ERR-DAG: '^c2'
*)

program p;
type c1 = (red, green); c2 = (blue, white);
var a: ^c1; b: ^c2;
begin new(a); new(b); a := b end.
