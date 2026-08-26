(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

{
line2
} x

(*
This CHECK block pins an exact source line, so keep the RUN block above
minimal -- reflowing it shifts every line below.

CHECK: 7:3: Identifier "x"
*)
