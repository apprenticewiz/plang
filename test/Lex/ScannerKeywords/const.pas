(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

const

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Const
*)
