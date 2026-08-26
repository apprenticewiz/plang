(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

(.1..3.)

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: LeftBracket
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: IntLit
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: DotDot
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: IntLit
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: RightBracket
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Eof
*)
