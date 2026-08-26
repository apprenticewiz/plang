(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

2147483648

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "2147483648"
*)
