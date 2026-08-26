(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

  foo

(*
This CHECK block pins an exact source line/column, so keep the RUN block
above minimal -- reflowing it shifts every line below.

CHECK: 5:3: Identifier "foo"
*)
