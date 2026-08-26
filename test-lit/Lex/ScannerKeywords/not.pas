(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

not

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Not
*)
