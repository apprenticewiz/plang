(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

var1

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "var1"
*)
