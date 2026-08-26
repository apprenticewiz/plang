(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

> <

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: GreaterThan
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: LessThan
*)
