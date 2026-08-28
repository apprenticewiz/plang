(*
Gluing requires no gap at all: a space between 'AB' and #65 stops it, giving
two separate StringLit tokens instead of one glued "ABA".
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

'AB' #65

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "AB"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: StringLit "A"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Eof
*)
