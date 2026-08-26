(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

x

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
