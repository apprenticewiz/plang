(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

1e3

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: RealLit "1e3"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
