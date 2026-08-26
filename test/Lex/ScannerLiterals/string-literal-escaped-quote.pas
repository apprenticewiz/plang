(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

'it''s'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "it's"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
