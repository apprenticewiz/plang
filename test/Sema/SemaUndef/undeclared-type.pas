(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; var x : MyType; begin end.

(*
CHECK: MyType
*)
