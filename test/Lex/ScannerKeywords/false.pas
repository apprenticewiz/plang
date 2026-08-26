(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

false

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: False
*)
