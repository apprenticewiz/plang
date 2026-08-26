(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR-DAG: 'a'
ERR-DAG: 'b'
*)

program p;
type a = record x: integer end;
     b = record y: real; z: char end;
var s: a; t: b;
begin s := t end.
