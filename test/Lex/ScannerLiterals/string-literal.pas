(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

'hello'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "hello"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
