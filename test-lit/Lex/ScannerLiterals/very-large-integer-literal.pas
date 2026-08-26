(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

99999999999999999

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "99999999999999999"
*)
