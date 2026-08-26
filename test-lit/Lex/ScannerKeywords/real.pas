(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

real

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Real
*)
