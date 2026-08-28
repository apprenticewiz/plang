(*
Lowercase hex digits work in '#$..' too. 0x4a = 74 = 'J'.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

#$4a

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "J"
*)
