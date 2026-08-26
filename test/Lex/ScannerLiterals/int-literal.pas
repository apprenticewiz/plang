(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

42

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "42"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
