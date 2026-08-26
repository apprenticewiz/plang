(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

1e

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "1"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "e"
*)
