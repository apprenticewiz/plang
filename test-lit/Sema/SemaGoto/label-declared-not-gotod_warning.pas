(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; label 10; var x : integer; begin 10: x := 0 end.

(*
CHECK: 10
*)
