(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; label 10, 10; begin end.

(*
CHECK: 10
*)
