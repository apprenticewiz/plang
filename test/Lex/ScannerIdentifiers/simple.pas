(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

myVar

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "myVar"
*)
