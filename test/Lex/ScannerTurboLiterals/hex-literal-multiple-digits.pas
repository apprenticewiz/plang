(*
A longer digit run: $1A2B = 1*4096 + 10*256 + 2*16 + 11 = 6699.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

$1A2B

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "6699"
*)
