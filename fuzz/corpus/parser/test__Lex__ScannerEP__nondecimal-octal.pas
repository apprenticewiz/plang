(*
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

8#377

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "255"
*)
