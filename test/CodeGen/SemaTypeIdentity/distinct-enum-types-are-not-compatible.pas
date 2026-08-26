(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR-DAG: 'c1'
ERR-DAG: 'c2'
*)

program p;
type c1 = (aa, bb); c2 = (dd, ee);
var u: c1; v: c2;
begin u := aa; v := dd; u := v end.
