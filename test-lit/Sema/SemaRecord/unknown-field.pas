(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type Rec = record x : integer end;
var myRec : Rec;
begin myRec.foo := 1 end.

(*
CHECK: foo
*)
