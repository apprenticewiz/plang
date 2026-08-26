(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; label 10; begin goto 10 end.

(*
CHECK: 10
*)
