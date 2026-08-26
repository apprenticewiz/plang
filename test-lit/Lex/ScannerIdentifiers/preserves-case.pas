(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

MyIdentifier

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "MyIdentifier"
*)
