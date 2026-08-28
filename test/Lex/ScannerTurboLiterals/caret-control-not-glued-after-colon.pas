(*
Colon is the other token deliberately left off startsExpression()'s
allow-list: it introduces a var/field/parameter/result type-denoter
(`var p: ^T`), which -- like type-declaration's Equal -- would collide with
a `^ctrl` literal if a fresh '^' after it were ever read as one.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

var p: ^Integer;

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Var "var"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "p"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Colon ":"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Integer "Integer"
*)
