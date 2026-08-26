(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

1..10

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "1"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: DotDot
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: IntLit "10"
*)
