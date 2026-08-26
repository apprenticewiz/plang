(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

p@ q^

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Caret
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Identifier
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Caret
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Eof
*)
