(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type R = record x : integer end;
var r : R;
begin with r do foo := 1 end.

(*
CHECK: foo
*)
