(*
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

16#ff

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "255"
*)
