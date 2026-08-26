(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

2.0 ** 8.0

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: RealLit
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: StarStar
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: RealLit
*)
