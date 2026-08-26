(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

x
y

(*
This CHECK block pins exact source line/column, so keep the RUN block
above minimal -- reflowing it shifts every line below.

CHECK: 5:1: Identifier "x"
CHECK-NEXT: 6:1: Identifier "y"
*)
