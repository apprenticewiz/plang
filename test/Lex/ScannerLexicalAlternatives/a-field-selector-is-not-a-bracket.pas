(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

r.x)

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Dot
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Identifier
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: RightParen
*)
