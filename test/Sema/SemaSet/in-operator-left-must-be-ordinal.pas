(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; var x : real; b : boolean;
begin b := x in [1, 2] end.

(*
CHECK: ordinal
*)
