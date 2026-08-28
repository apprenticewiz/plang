(*
'(' is likewise on startsExpression()'s allow-list: a `^ctrl` literal may
open a call argument, same as any other literal.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

writeln(^Z)

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "writeln"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: LeftParen "("
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: StringLit
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: RightParen ")"
*)
