(*
A comment before an identifier does not throw off column tracking: x
begins at column 7, right after the brace comment closes.  This CHECK
block pins an exact source line/column, so keep the RUN block below
minimal -- reflowing it shifts every line below.

RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

{ c } x

(*
CHECK: 10:7: Identifier "x"
*)
