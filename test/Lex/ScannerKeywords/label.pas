(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

label

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Label
*)
