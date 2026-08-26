(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

div

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Div
*)
