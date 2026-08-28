(*
Gluing (see the string-gluing-*.pas fixtures) is explicitly scoped to
string / #code / ^ctrl fragments only.  $hex is a completely different
token kind (IntLit, not StringLit) and never participates: a string
immediately following one is always its own separate token.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

$FF'AB'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "255"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: StringLit "AB"
*)
