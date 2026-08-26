(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

1.5E-2 2.5e+10 6E4

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: RealLit "1.5E-2"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: RealLit "2.5e+10"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: RealLit "6E4"
*)
