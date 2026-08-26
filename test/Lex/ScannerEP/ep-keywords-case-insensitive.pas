(*
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

AND_THEN Or_Else OTHERWISE Module

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: AndThen
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: OrElse
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Otherwise
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Module
*)
