(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

while

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: While
*)
