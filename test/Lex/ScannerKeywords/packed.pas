(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

packed

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Packed
*)
