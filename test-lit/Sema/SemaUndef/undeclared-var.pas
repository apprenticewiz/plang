(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; begin x := 1 end.

(*
CHECK: x
*)
