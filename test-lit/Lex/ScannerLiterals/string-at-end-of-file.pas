(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

'x'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "x"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
