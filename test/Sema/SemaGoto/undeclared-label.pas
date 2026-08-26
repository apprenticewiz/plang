(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; begin goto 99 end.

(*
CHECK: 99
*)
