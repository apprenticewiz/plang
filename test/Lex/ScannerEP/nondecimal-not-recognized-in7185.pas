(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

16#ff

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "16"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "ff"
*)
